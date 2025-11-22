# pip install plugllm
from plugllm import config, chat
config(provider="groq", api_key="GROQ_API_KEY", model="openai/gpt-oss-120b")
while i := input("Ask: "): print(chat(i))
