# pip install limma pyvoicekit

from pyvoicekit import speak, listen
from limma import Limma, LimmaConfig
import time

# ---------------- CONFIGURATION ----------------
config = LimmaConfig(
    esp_ip="YOUR_ESP_IP", # ENTER YOUR ESP DEVICE STA IP ADDRESS
    application_type="home",
    device_map={
        "room light": "ch01on | ch01off",
        "bedroom fan": "ch02on | ch02off",
        "kitchen light": "ch03on | ch03off",
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

# Greet the user
speak("Hello! I am your home assistant. I am ready to take commands.")

# ---------------- MAIN LOOP ----------------
while True:
    try:
        # Listen for voice input
        command = listen(timeout=8)  # listen for 8 seconds
        if not command:
            continue  # No input, continue listening

        print(f"🎤 You said: {command}")

        # Execute command via LIMMA
        success, reply = limma.execute_command(command)

        if success:
            print(f"✅ {reply}")
            speak(reply)
        else:
            print("❌ Could not execute the command.")
            speak("Sorry, I could not understand or execute that command.")

        # Optional: small pause before next command
        time.sleep(1)

    except KeyboardInterrupt:
        print("👋 Exiting LIMMA assistant...")
        speak("Goodbye!")
        break

    except Exception as e:
        print(f"⚠️ Error: {e}")
        speak("Something went wrong, please try again.")
        time.sleep(1)
