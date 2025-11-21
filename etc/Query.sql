CREATE DATABASE stress_monitor;
USE stress_monitor;

CREATE TABLE monitoring (
    id INT AUTO_INCREMENT PRIMARY KEY,
    spo2 INT NOT NULL,
    bpm INT NOT NULL,
    status ENUM('Normal', 'Gradual Stress', 'Stress Spike', 'No Data') DEFAULT 'No Data',
    recorded VARCHAR(8) NOT NULL,
    emotion VARCHAR(50) DEFAULT '----',
    confidence FLOAT DEFAULT 0
);

-- Sample test data
INSERT INTO monitoring (spo2, bpm, status, recorded, emotion, confidence) VALUES
(98, 72, 'Normal', '08:15:00', 'happy', 0.87),
(97, 85, 'Gradual Stress', '08:20:00', 'angry', 0.60),
(95, 105, 'Stress Spike', '08:25:00', 'sad', 0.92),
(0, 0, 'No Data', '08:30:00', '----', 0);

SELECT * from monitoring
DROP TABLE IF EXISTS monitoring;