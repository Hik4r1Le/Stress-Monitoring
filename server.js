const express = require('express');
const mysql = require('mysql2');
const cors = require('cors');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3000;
const path = require('path');

app.use(cors());
app.use(express.json());

// ------------------ DATABASE ------------------
const db = mysql.createPool({
    host: process.env.DB_HOST,
    user: process.env.DB_USER,
    password: process.env.DB_PASSWORD,
    database: process.env.DB_NAME,
    waitForConnections: true,
    connectionLimit: 10,
    queueLimit: 0
}).promise();

// ------------------ API ROUTES ------------------

// Fetch latest data
app.get('/api/latest', async (req, res) => {
    try {
        const [rows] = await db.query(`
            SELECT spo2, bpm, status, recorded, emotion, confidence
            FROM monitoring
            ORDER BY id DESC
            LIMIT 1
        `);

        if (rows.length === 0) {
            return res.json({ spo2: 0, bpm: 0, status: 'No Data', recorded: '--', emotion: '----', confidence: 0 });
        }

        const row = rows[0];
        res.json({
            spo2: row.spo2,
            bpm: row.bpm,
            status: row.status,
            recorded: row.recorded,
            emotion: row.emotion || '----',
            confidence: row.confidence || 0
        });
    } catch (err) {
        console.error(err);
        res.status(500).json({ spo2: 0, bpm: 0, status: 'Error', recorded: '--', emotion: '----', confidence: 0 });
    }
});

// Fetch history for chart
app.get('/api/history', async (req, res) => {
    try {
        const [rows] = await db.query(`
            SELECT bpm, recorded 
            FROM monitoring 
            ORDER BY recorded DESC LIMIT 20
        `);
        res.json(rows.reverse());
    } catch (err) {
        console.error(err);
        res.status(500).json({ error: "Database error" });
    }
});

// Receive data from ESP32
// Receive FULL data from ESP32 (vitals + emotion in one go)
app.post('/api/data', async (req, res) => {
    const { spo2, bpm, status, time, emotion = '----', confidence = 0 } = req.body;

    if (spo2 === undefined || bpm === undefined || !status || !time) {
        return res.status(400).json({ error: "Missing vitals" });
    }

    try {
        await db.query(
            `INSERT INTO monitoring (spo2, bpm, status, recorded, emotion, confidence)
             VALUES (?, ?, ?, ?, ?, ?)`,
            [spo2, bpm, status, time, emotion, confidence]
        );

        console.log(`Full data saved → ${time} | BPM:${bpm} | SpO2:${spo2}% | ${status} | Emotion: ${emotion}`);
        res.json({ success: true });
    } catch (err) {
        console.error("DB error:", err);
        res.status(500).json({ error: "Save failed" });
    }
});

// ------------------ STATIC FILES (SERVE LAST!) ------------------

// Serve index.html, style.css, script.js
//app.use(express.static(path.join(__dirname, '..')));
app.use(express.static(__dirname));
app.get('/*', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// ------------------ START SERVER ------------------
app.listen(PORT, '0.0.0.0', () => {
    console.log(`Server running on port ${PORT}`);
});
