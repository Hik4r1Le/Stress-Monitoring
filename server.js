const express = require('express');
const cors = require('cors');
const path = require('path');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3000;

app.use(cors());
app.use(express.json());

// ====================== UNIVERSAL DB CONNECTION ======================
let query, dbClient;

if (process.env.DB_HOST && process.env.DB_HOST.includes('postgres')) {
  // === Render: PostgreSQL ===
  const { Pool } = require('pg');
  const pool = new Pool({
    host: process.env.DB_HOST,
    port: 5432,
    user: process.env.DB_USER,
    password: process.env.DB_PASSWORD,
    database: process.env.DB_NAME,
    ssl: { rejectUnauthorized: false },
  });
  dbClient = pool;
  query = async (sql, params = []) => {
    const res = await pool.query(sql.replace(/\?/g, (m, i) => `$${i + 1}`), params);
    return res; // { rows: [...], rowCount, ... }
  };
  console.log('Connected to Render PostgreSQL');
} else {
  // === Local: MySQL ===
  const mysql = require('mysql2/promise');
  const pool = mysql.createPool({
    host: process.env.DB_HOST || 'localhost',
    user: process.env.DB_USER || 'root',
    password: process.env.DB_PASSWORD || '',
    database: process.env.DB_NAME || 'stress_monitor',
    waitForConnections: true,
    connectionLimit: 10,
  });
  dbClient = pool;
  query = async (sql, params = []) => {
    const [rows] = await pool.query(sql, params);
    return { rows }; // mimic pg result shape
  };
  console.log('Connected to local MySQL');
}

// ====================== API ROUTES ======================

app.get('/api/latest', async (req, res) => {
  try {
    const result = await query(`
      SELECT spo2, bpm, status, recorded, emotion, confidence
      FROM monitoring ORDER BY id DESC LIMIT 1
    `);
    const row = result.rows[0] || { spo2: 0, bpm: 0, status: 'No Data', recorded: '--', emotion: '----', confidence: 0 };
    res.json({
      spo2: row.spo2 || 0,
      bpm: row.bpm || 0,
      status: row.status || 'No Data',
      recorded: row.recorded || '--',
      emotion: row.emotion || '----',
      confidence: parseFloat(row.confidence) || 0
    });
  } catch (err) {
    console.error(err);
    res.status(500).json({ spo2: 0, bpm: 0, status: 'Error', recorded: '--', emotion: '----', confidence: 0 });
  }
});

app.get('/api/history', async (req, res) => {
  try {
    const result = await query(`
      SELECT bpm, recorded FROM monitoring ORDER BY recorded DESC LIMIT 20
    `);
    res.json(result.rows.reverse());
  } catch (err) {
    console.error(err);
    res.status(500).json([]);
  }
});

app.post('/api/data', async (req, res) => {
  const { spo2, bpm, status, time, emotion = '----', confidence = 0 } = req.body;

  if (spo2 === undefined || bpm === undefined || !status || !time) {
    return res.status(400).json({ error: "Missing vitals" });
  }

  try {
    await query(
      `INSERT INTO monitoring (spo2, bpm, status, recorded, emotion, confidence)
       VALUES ($1, $2, $3, $4, $5, $6)`,
      [spo2, bpm, status, time, emotion, confidence]
    );
    console.log(`Saved → ${time} | SpO2:${spo2}% | BPM:${bpm} | ${status} | ${emotion}`);
    res.json({ success: true });
  } catch (err) {
    console.error("DB insert error:", err);
    res.status(500).json({ error: "Save failed" });
  }
});

// ====================== SERVE FRONTEND ======================
app.use(express.static(__dirname));
app.get('*', (req, res) => {
  res.sendFile(path.join(__dirname, 'index.html'));
});

// ====================== START ======================
app.listen(PORT, '0.0.0.0', () => {
  console.log(`Server running on port ${PORT}`);
});