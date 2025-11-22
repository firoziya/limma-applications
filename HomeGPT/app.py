# pip install flask limma

from flask import Flask, render_template, request, jsonify, session
from limma import Limma, LimmaConfig
import time
import uuid
import os

app = Flask(__name__)
app.secret_key = os.urandom(24)

# ---------------- CONFIGURATION ----------------
config = LimmaConfig(
    esp_ip="YOUR_ESP_IP", # ENTER YOUR ESP DEVICE STA IP ADDRESS
    application_type="home",
    device_map={
        "light": "ch01on | ch01off",
        "fan": "ch02on | ch02off",
        "lamp": "ch03on | ch03off",
        "tv": "ch04on | ch04off",
        "temperature sensor": "getTemp",
        "humidity sensor": "getHum",
        "servo motor 1": "s1_<degrees>",
        "servo motor 2": "s2_<degrees>"
    },
    api_key="YOUR_LIMMA_API_KEY", # GET YOUR API KEY FROM https://limma.live
    reply=True
)

# Initialize LIMMA
limma = Limma(config)

# Store chat histories (in production, use a database)
chat_histories = {}

@app.route('/')
def index():
    # Generate a unique session ID for each user
    if 'user_id' not in session:
        session['user_id'] = str(uuid.uuid4())
    
    user_id = session['user_id']
    
    # Initialize chat history for this user if it doesn't exist
    if user_id not in chat_histories:
        chat_histories[user_id] = [
            {"sender": "bot", "message": "Hello! I am your home assistant. I am ready to take commands.", "timestamp": time.time()}
        ]
    
    return render_template('index.html', chat_history=chat_histories[user_id])

@app.route('/send_command', methods=['POST'])
def send_command():
    user_id = session.get('user_id')
    if not user_id:
        return jsonify({"error": "Session expired"}), 400
    
    data = request.get_json()
    command = data.get('command', '').strip()
    
    if not command:
        return jsonify({"error": "Empty command"}), 400
    
    # Add user message to chat history
    if user_id not in chat_histories:
        chat_histories[user_id] = []
    
    chat_histories[user_id].append({
        "sender": "user", 
        "message": command, 
        "timestamp": time.time()
    })
    
    try:
        # Execute command via LIMMA
        success, reply = limma.execute_command(command)
        
        if success:
            bot_response = reply
        else:
            bot_response = "❌ Sorry, I could not understand or execute that command."
        
        # Add bot response to chat history
        chat_histories[user_id].append({
            "sender": "bot", 
            "message": bot_response, 
            "timestamp": time.time()
        })
        
        return jsonify({
            "success": True,
            "response": bot_response,
            "chat_history": chat_histories[user_id]
        })
        
    except Exception as e:
        error_message = f"⚠️ Error: {str(e)}"
        chat_histories[user_id].append({
            "sender": "bot", 
            "message": error_message, 
            "timestamp": time.time()
        })
        
        return jsonify({
            "success": False,
            "response": error_message,
            "chat_history": chat_histories[user_id]
        })

@app.route('/clear_chat', methods=['POST'])
def clear_chat():
    user_id = session.get('user_id')
    if user_id and user_id in chat_histories:
        # Clear chat history but keep the initial greeting
        chat_histories[user_id] = [
            {"sender": "bot", "message": "Hello! I am your home assistant. I am ready to take commands.", "timestamp": time.time()}
        ]
    
    return jsonify({"success": True})

if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5000)
