from flask import Flask, request, jsonify, send_file
from PIL import Image
import pytesseract
import io
import os
import re
import json
import requests
from typing import Optional, Dict, Any

app = Flask(__name__)

SCRYFALL_BASE = "https://api.scryfall.com"

# ---------------------------------------------------------
# STORE LAST SCANNED CARD (Option B)
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
# OCR PARSER (extract set + collector number)
# ---------------------------------------------------------
def parse_ocr_text(text: str):
    lines = text.splitlines()

    number = None
    number_line_index = None

    for i, line in enumerate(lines):
        m = re.search(r"([0-9]{1,4})", line)
        if m:
            number = m.group(1).lstrip("0") or "0"
            number_line_index = i
            break

    if not number:
        return None, None

    set_code = None

    for i, line in enumerate(lines):
        if i == number_line_index:
            continue

        matches = re.findall(r"[A-Za-z]{2,5}", line)
        if matches:
            set_code = matches[0].lower()
            break

    if not set_code:
        return None, None

    return set_code, number


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
        extracted_text = pytesseract.image_to_string(image)

        return jsonify({
            'status': 'success',
            'text': extracted_text,
            'filename': image_file.filename
        }), 200

    except Exception as e:
        return jsonify({'error': str(e)}), 500


# ---------------------------------------------------------
# OCR + PARSE + SCRYFALL LOOKUP ENDPOINT
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
        extracted_text = pytesseract.image_to_string(image)

        set_code, number = parse_ocr_text(extracted_text)

        if not set_code or not number:
            return jsonify({
                "status": "success",
                "raw_text": extracted_text,
                "error": "Could not parse set code or collector number"
            }), 200

        card = get_card_by_set_and_number(set_code, number)

        # ---------------------------------------------------------
        # SAVE LAST SCANNED CARD (Option B)
        # ---------------------------------------------------------
        global last_card_result
        last_card_result = {
            "raw_text": extracted_text,
            "set": set_code,
            "number": number,
            "scryfall_code": f"{set_code}/{number}",
            "card": card
        }

        return jsonify({
            "status": "success",
            "raw_text": extracted_text,
            "set": set_code,
            "number": number,
            "scryfall_code": f"{set_code}/{number}",
            "card": card
        }), 200

    except Exception as e:
        return jsonify({'error': str(e)}), 500


# ---------------------------------------------------------
# NEW ENDPOINT: RETURN LAST SCANNED CARD
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
