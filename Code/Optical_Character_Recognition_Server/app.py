from flask import Flask, request, jsonify, send_file
from PIL import Image, ImageEnhance, ImageFilter
import pytesseract
import io
import os
import re
import json
import requests
from typing import Optional, Dict, Any

import cv2
import numpy as np

app = Flask(__name__)

SCRYFALL_BASE = "https://api.scryfall.com"

# ---------------------------------------------------------
# STORE LAST SCANNED CARD
# ---------------------------------------------------------
last_card_result = None


# ---------------------------------------------------------
# SCRYFALL FETCH HELPER
# ---------------------------------------------------------
def _fetch_json(url: str, session: Optional[requests.Session] = None, params: Optional[Dict[str, Any]] = None):
    sess = session or requests.Session()
    try:
        resp = sess.get(url, params=params, timeout=10)
    except requests.RequestException as exc:
        return {"error": f"Scryfall did not respond: {exc}"}

    if not resp.ok:
        try:
            body = resp.json()
            detail = body.get("details") or body.get("message") or json.dumps(body)
        except Exception:
            detail = resp.text
        return {"error": f"Scryfall returned status {resp.status_code}: {detail}"}

    try:
        return resp.json()
    except Exception as exc:
        return {"error": f"Failed to parse Scryfall response JSON: {exc}"}


# ---------------------------------------------------------
# SCRYFALL LOOKUP USING SET + NUMBER
# ---------------------------------------------------------
def get_card_by_set_and_number(set_code: str, number: str, session: Optional[requests.Session] = None):
    set_code = set_code.lower().strip()
    number = number.strip().lstrip("0") or "0"
    url = f"{SCRYFALL_BASE}/cards/{set_code}/{number}"
    return _fetch_json(url, session=session)


# ---------------------------------------------------------
# IMAGE PREPROCESSING
# Tries multiple strategies and returns the best OCR result
# ---------------------------------------------------------
def preprocess_for_ocr(image: Image.Image) -> list[np.ndarray]:
    """
    Returns a list of preprocessed cv2 images to try OCR on.
    Handles both light-on-dark (MTG footer) and dark-on-light text.
    """
    # Convert PIL to OpenCV BGR
    cv_img = cv2.cvtColor(np.array(image.convert("RGB")), cv2.COLOR_RGB2BGR)
    gray = cv2.cvtColor(cv_img, cv2.COLOR_BGR2GRAY)

    # Upscale — small text needs this
    scale = 3
    gray = cv2.resize(gray, None, fx=scale, fy=scale, interpolation=cv2.INTER_CUBIC)

    variants = []

    # 1. CLAHE (adaptive contrast) — good for uneven lighting
    clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))
    clahe_img = clahe.apply(gray)
    variants.append(clahe_img)

    # 2. Inverted Otsu threshold — best for white text on dark background (MTG footer)
    _, otsu_inv = cv2.threshold(clahe_img, 0, 255, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
    variants.append(otsu_inv)

    # 3. Normal Otsu threshold — best for dark text on light background
    _, otsu = cv2.threshold(clahe_img, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    variants.append(otsu)

    # 4. Adaptive threshold inverted — good for shadows/gradients
    adaptive_inv = cv2.adaptiveThreshold(
        gray, 255,
        cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
        cv2.THRESH_BINARY_INV,
        31, 10
    )
    variants.append(adaptive_inv)

    # 5. Denoised + sharpened
    denoised = cv2.fastNlMeansDenoising(clahe_img, h=20)
    kernel = np.array([[0, -1, 0], [-1, 5, -1], [0, -1, 0]])
    sharpened = cv2.filter2D(denoised, -1, kernel)
    variants.append(sharpened)

    return variants


def run_ocr_variants(image: Image.Image, psm_modes: list[int] = [6, 7, 11, 13]) -> str:
    """
    Runs OCR across multiple preprocessed image variants and PSM modes.
    Returns the result with the most parseable content.
    """
    variants = preprocess_for_ocr(image)
    best_text = ""
    best_score = -1

    for variant in variants:
        pil_variant = Image.fromarray(variant)
        for psm in psm_modes:
            config = f"--oem 3 --psm {psm}"
            try:
                text = pytesseract.image_to_string(pil_variant, config=config)
                text = text.strip()

                # Score: prefer results that contain digits and letters (set codes / numbers)
                score = len(re.findall(r'[A-Za-z0-9]', text))
                if score > best_score:
                    best_score = score
                    best_text = text
            except Exception:
                continue

    return best_text


# ---------------------------------------------------------
# OCR PARSER (extract set + collector number)
# ---------------------------------------------------------
def parse_ocr_text(text: str):
    """
    Parses OCR output to find:
      - Collector number: e.g. '191', '0191', 'M 0191'
      - Set code: 2–5 uppercase/lowercase letters (e.g. 'TDC', 'M21')

    MTG footer format: "<collector_number> <set_code> • <language> <artist>"
    Example: "M 0191 TDC • EN ► Lius Lasahido"
    """
    # Normalize: remove bullet/middot/arrow noise
    cleaned = re.sub(r'[•·►▶»\*]', ' ', text)
    cleaned = re.sub(r'\s+', ' ', cleaned).strip()

    # --- Collector number ---
    # Matches optional letter prefix (M, P, etc.) followed by 1-4 digits
    number_match = re.search(r'\b([A-Za-z]?\s*\d{1,4})\b', cleaned)
    number = None
    if number_match:
        raw_num = number_match.group(1)
        # Extract only the digits
        digits = re.sub(r'[^0-9]', '', raw_num)
        number = digits.lstrip("0") or "0"

    # --- Set code ---
    # Typically 2–5 uppercase letters, appears near the collector number
    # Exclude common noise words: EN, JP, FR, DE (language codes)
    LANG_CODES = {"EN", "JP", "FR", "DE", "IT", "ES", "PT", "KO", "RU", "CS", "CT"}

    set_code = None
    candidates = re.findall(r'\b([A-Za-z]{2,5})\b', cleaned)
    for candidate in candidates:
        upper = candidate.upper()
        if upper not in LANG_CODES:
            set_code = candidate.lower()
            break

    return set_code, number


# ---------------------------------------------------------
# CROP BOTTOM STRIP (where MTG footer lives)
# ---------------------------------------------------------
def crop_footer(image: Image.Image, fraction: float = 0.15) -> Image.Image:
    """
    Crops the bottom `fraction` of the image — where the set code,
    collector number, and artist line appear on MTG cards.
    """
    w, h = image.size
    top = int(h * (1.0 - fraction))
    return image.crop((0, top, w, h))


# ---------------------------------------------------------
# OCR ONLY ENDPOINT
# ---------------------------------------------------------
@app.route('/ocr', methods=['POST'])
def perform_ocr():
    try:
        if 'image' not in request.files:
            return jsonify({'error': 'No image file provided'}), 400

        image_file = request.files['image']
        image_data = image_file.read()

        with open("debug.jpg", "wb") as f:
            f.write(image_data)

        image = Image.open(io.BytesIO(image_data))

        # Try full image first, then footer crop
        full_text = run_ocr_variants(image, psm_modes=[6, 11])
        footer = crop_footer(image, fraction=0.15)
        footer_text = run_ocr_variants(footer, psm_modes=[7, 6, 13])

        return jsonify({
            'status': 'success',
            'full_text': full_text,
            'footer_text': footer_text,
            'filename': image_file.filename
        }), 200

    except Exception as e:
        return jsonify({'error': str(e)}), 500


# ---------------------------------------------------------
# OCR + SCRYFALL LOOKUP
# ---------------------------------------------------------
@app.route('/ocr-and-lookup', methods=['POST'])
def ocr_and_lookup():
    try:
        if 'image' not in request.files:
            return jsonify({'error': 'No image file provided'}), 400

        image_file = request.files['image']
        image_data = image_file.read()

        with open("debug.jpg", "wb") as f:
            f.write(image_data)

        image = Image.open(io.BytesIO(image_data))

        # --- Step 1: OCR the footer strip (most reliable for set/number) ---
        footer = crop_footer(image, fraction=0.15)
        footer_text = run_ocr_variants(footer, psm_modes=[7, 6, 13])

        # --- Step 2: Also OCR full image as fallback ---
        full_text = run_ocr_variants(image, psm_modes=[6, 11])

        # --- Step 3: Try parsing footer first, then full image ---
        set_code, number = parse_ocr_text(footer_text)
        source = "footer"

        if not set_code or not number:
            set_code, number = parse_ocr_text(full_text)
            source = "full_image"

        raw_text_info = {
            "footer_ocr": footer_text,
            "full_ocr": full_text,
            "parse_source": source
        }

        if not set_code or not number:
            return jsonify({
                "status": "partial",
                "raw_text": raw_text_info,
                "error": "Could not parse set code or collector number from OCR output"
            }), 200

        # --- Step 4: Lookup on Scryfall ---
        card = get_card_by_set_and_number(set_code, number)

        global last_card_result
        last_card_result = {
            "raw_text": raw_text_info,
            "set": set_code,
            "number": number,
            "scryfall_code": f"{set_code}/{number}",
            "card": card
        }

        return jsonify(last_card_result), 200

    except Exception as e:
        return jsonify({'error': str(e)}), 500


# ---------------------------------------------------------
# RETURN LAST SCANNED CARD
# ---------------------------------------------------------
@app.route('/last-card', methods=['GET'])
def get_last_card():
    global last_card_result
    if last_card_result is None:
        return jsonify({"error": "No card scanned yet"}), 404
    return jsonify(last_card_result), 200


# ---------------------------------------------------------
# HEALTH + DEBUG
# ---------------------------------------------------------
@app.route('/health', methods=['GET'])
def health_check():
    return jsonify({'status': 'OCR Server is running'}), 200


@app.route('/debug-image')
def debug_image():
    return send_file("debug.jpg", mimetype="image/jpeg")


# ---------------------------------------------------------
# MAIN
# ---------------------------------------------------------
if __name__ == '__main__':
    port = int(os.environ.get("PORT", 5000))
    app.run(debug=False, host='0.0.0.0', port=port)
