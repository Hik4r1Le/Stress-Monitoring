# 1. Kết nối SSH vào server
ssh -i "C:\Users\Admin\.ssh\key-pair.pem" ubuntu@<IP_SERVER>

# 2. Di chuyển vào thư mục dự án
cd Emotion

# 3. Tạo môi trường ảo Python
python3 -m venv venv

# 4. Kích hoạt môi trường ảo
source venv/bin/activate

# 5. Khởi chạy API bằng Uvicorn
uvicorn main:app --host 0.0.0.0 --port 8000
