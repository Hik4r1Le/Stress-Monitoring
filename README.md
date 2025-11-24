# Training-AI-for-emotion
ssh -i "C:\Users\Admin\.ssh\key-pair.pem" ubuntu@<IP_SERVER>
cd Emotion
python3 -m venv venv
source venv/bin/activate
uvicorn main:app --host 0.0.0.0 --port 8000