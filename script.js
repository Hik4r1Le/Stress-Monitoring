document.addEventListener('DOMContentLoaded', function () {
    const spo2Value = document.getElementById('spo2-value');
    const hrValue = document.getElementById('hr-value');
    const statusValue = document.getElementById('status-value');
    const emotionResult = document.getElementById('emotion-result');
    const gaugeFill = document.querySelector('.gauge-fill');

    let chartInstance = null;
    const MAX_CHART_ENTRIES = 10;
    const POLLING_INTERVAL = 5000; // 5 seconds

    // ------------------ GAUGE ------------------
    function updateSpo2Gauge(value) {
        const percentage = parseFloat(value) || 0;
        const clampedValue = Math.min(Math.max(percentage, 0), 100);
        const arcLength = 251.2;
        const offset = arcLength * (1 - clampedValue / 100);
        gaugeFill.style.strokeDashoffset = offset;
        spo2Value.textContent = `${clampedValue}%`;
    }

    // ------------------ FETCH LATEST DATA ------------------
    async function fetchLatestData() {
        try {
            const response = await fetch('/api/latest');
            if (!response.ok) throw new Error('Failed');
            const data = await response.json();
            return data;
        } catch (err) {
            console.error('Fetch latest failed:', err);
            return null;
        }
    }

    // ------------------ UPDATE UI ------------------
async function updateUI() {
    const latestData = await fetchLatestData();

    if (latestData) {
        updateSpo2Gauge(latestData.spo2 || 0);
        hrValue.textContent = latestData.bpm > 0 ? `${latestData.bpm} bpm` : '-- bpm';
        statusValue.textContent = `Status: ${latestData.status || 'Normal'}`;

        // Emotion from backend
        if (latestData.emotion && latestData.emotion !== '----') {
            emotionResult.textContent = `${latestData.emotion} - ${Math.round(latestData.confidence * 100)}%`;
        } else {
            emotionResult.textContent = "Waiting for face...";
        }
    } else {
        updateSpo2Gauge(0);
        hrValue.textContent = '-- bpm';
        statusValue.textContent = 'Status: Offline';
        emotionResult.textContent = "No connection";
    }

    updateChart();
}

    // ------------------ CHART (Visible from start + 30–220 BPM) ------------------
    function initializeChart(labels = [], data = []) {
        const ctx = document.getElementById('stressChart').getContext('2d');
        if (chartInstance) chartInstance.destroy();

        // Fill with placeholders if empty
        while (labels.length < MAX_CHART_ENTRIES) labels.push("--:--");
        while (data.length < MAX_CHART_ENTRIES) data.push(null);

        chartInstance = new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [{
                    label: 'Heart Rate',
                    data: data,
                    borderColor: '#e74c3c',
                    backgroundColor: 'rgba(231, 76, 60, 0.3)',
                    borderWidth: 5,
                    pointBackgroundColor: '#e74c3c',
                    pointRadius: 6,
                    pointHoverRadius: 9,
                    tension: 0.4,
                    fill: true
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        callbacks: {
                            label: (ctx) => ctx.parsed.y ? `BPM: ${ctx.parsed.y}` : 'No data'
                        }
                    }
                },
                scales: {
                    y: {
                        min: 30,
                        max: 220,
                        ticks: {
                            stepSize: 20,
                            color: '#ecf0f1',
                            font: { size: 14, weight: 'bold' }
                        },
                        grid: { color: 'rgba(255,255,255,0.2)' },
                        title: {
                            display: true,
                            text: 'BPM',
                            color: '#1abc9c',
                            font: { size: 18, weight: 'bold' }
                        }
                    },
                    x: {
                        ticks: {
                            color: '#ecf0f1',
                            font: { size: 13 }
                        },
                        grid: { color: 'rgba(255,255,255,0.1)' },
                        title: {
                            display: true,
                            text: 'Time',
                            color: '#1abc9c',
                            font: { size: 18, weight: 'bold' }
                        }
                    }
                }
            }
        });
    }
    async function updateChart() {
        try {
            const res = await fetch('/api/history');
            const history = await res.json();

            const labels = history.length > 0
                ? history.map(h => h.recorded)  // <-- use recorded string directly
                : [];

            const data = history.length > 0 ? history.map(h => h.bpm) : [];

            if (!chartInstance) {
                initializeChart(labels, data);
            } else {
                const paddedLabels = [...labels.slice(-MAX_CHART_ENTRIES)];
                const paddedData = [...data.slice(-MAX_CHART_ENTRIES)];

                while (paddedLabels.length < MAX_CHART_ENTRIES) paddedLabels.push("--:--");
                while (paddedData.length < MAX_CHART_ENTRIES) paddedData.push(null);

                chartInstance.data.labels = paddedLabels;
                chartInstance.data.datasets[0].data = paddedData;
                chartInstance.update('quiet');
            }
        } catch (err) {
            console.error('Chart update failed:', err);
            if (!chartInstance) initializeChart();
        }
    }
    
    // ------------------ INITIALIZE ------------------
    initializeChart();     // ← Chart appears IMMEDIATELY
    updateUI();            // ← Load first data

    // ------------------ POLLING ------------------
    setInterval(() => {
        updateUI();
        updateChart();
    }, POLLING_INTERVAL);
});