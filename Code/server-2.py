
from flask import Flask, request, jsonify, render_template

app = Flask(__name__)

# ================= MICROPHONE DATA =================

latest_data = {
    "samples": [],
    "amplitude": 0,
    "peak": 0
}

# ================= WEBSITE =================

@app.route("/")
def home():
    return render_template("index.html")


# ================= RECEIVE ESP32 DATA =================

@app.route("/data", methods=["POST"])
def receive_data():

    global latest_data

    try:
        latest_data = request.get_json(force=True)

        print(
            f"Amplitude: {latest_data['amplitude']} | "
            f"Peak: {latest_data['peak']} | "
            f"Samples: {len(latest_data['samples'])}"
        )

        return jsonify({
            "success": True
        })

    except Exception as e:

        print("Error:", e)

        return jsonify({
            "success": False,
            "error": str(e)
        }), 400


# ================= SEND DATA TO WEBSITE =================

@app.route("/status")
def status():

    return jsonify(latest_data)


# ================= START SERVER =================

if __name__ == "__main__":

    print("===================================")
    print(" Flask server started")
    print(" Open: http://127.0.0.1:2000")
    print("===================================")

    app.run(
        host="0.0.0.0",
        port=2000,
        debug=True
    )