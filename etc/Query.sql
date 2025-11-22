// create-table.js  ← final version that always talks
const { Client } = require('pg');

console.log('Connecting to Render PostgreSQL...');

const client = new Client({
  connectionString: 'postgresql://stress_user:GhvkDKIDV9tcqB0qhY70iFavcUuWzvuT@dpg-d4gignn5r7bs73b9fpa0-a.oregon-postgres.render.com:5432/stress_monitor_4nn8',
  ssl: { rejectUnauthorized: false }
});

client.connect((err) => {
  if (err) {
    console.error('Connection failed:', err.message);
    return;
  }
  console.log('Connected! Creating table...');

  client.query(`
    CREATE TABLE IF NOT EXISTS monitoring (
        id SERIAL PRIMARY KEY,
        spo2 INT NOT NULL,
        bpm INT NOT NULL,
        status VARCHAR(20) DEFAULT 'No Data' 
               CHECK (status IN ('Normal', 'Gradual Stress', 'Stress Spike', 'No Data')),
        recorded VARCHAR(8) NOT NULL,
        emotion VARCHAR(50) DEFAULT '----',
        confidence REAL DEFAULT 0
    );
  `, (err, res) => {
    if (err) {
      console.error('Query failed:', err.message);
    } else {
      console.log('Table created/verified successfully!');
    }
    client.end();
  });
});