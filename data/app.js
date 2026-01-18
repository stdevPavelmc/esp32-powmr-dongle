let namesData = {};
let pollInterval = 5000; // 5 seconds
let timeoutId = null;

async function fetchNames() {
    try {
        const res = await fetch('/names.json');
        namesData = await res.json();
    } catch (e) {
        console.error('Failed to load names:', e);
    }
}

async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        const data = await res.json();
        
        // update data
        renderDashboard(data);
        updateFooter();

        // // select the read_interval
        // if (data.inverter && data.inverter.read_interval_ms) {
        //     const newInterval = data.inverter.read_interval_ms / 1000; // seconds
        //     if (newInterval !== pollInterval) {
        //         pollInterval = newInterval;
        //         schedulePoll();
        //     }
        // }
    } catch (e) {
        console.error('Failed to fetch status:', e);
        document.getElementById('lastUpdate').textContent = 'Connection error';
    }
}

function formatValue(val, key = '', sectionKey = '') {
    if (val === null || val === undefined)
        return '-';

    // Special formatting for autonomy and uptime (minutes to days:hours:minutes)
    if ((key === 'autonomy' || key === 'uptime') && sectionKey === 'inverter') {
        return formatAutonomy(val);
    }

    if (Number.isInteger(val) || typeof val === 'number' && val % 1 !== 0)
        return parseFloat(val).toFixed(1);

    // final resort, return the value
    return val;
}

function formatAutonomy(minutes) {
    if (!Number.isInteger(minutes) || minutes < 0) {
        return minutes.toString();
    }

    const days = Math.floor(minutes / (24 * 60));
    const hours = Math.floor((minutes % (24 * 60)) / 60);
    const mins = minutes % 60;

    let result = '';

    // Only show days if there are any days
    if (days > 0) {
        result += days + 'd:';
    }

    result += hours + 'h:' + mins + 'm';

    return result;
}

function createCard(key, value, sectionKey) {
    const card = document.createElement('div');
    card.className = 'card';
    
    // Access nested metadata properly
    const sectionMeta = namesData[sectionKey] || {};
    const fieldMeta = sectionMeta[key] || {};
    const name = fieldMeta.name || key;
    const unit = fieldMeta.unit || '';
    const desc = fieldMeta.description || '';
    
    const title = document.createElement('div');
    title.className = 'card-title';
    title.textContent = name;
    if (desc) title.title = desc;
    
    const val = document.createElement('div');
    val.className = 'card-value';
    val.textContent = formatValue(value, key, sectionKey);
    
    const unitEl = document.createElement('span');
    unitEl.className = 'card-unit';
    unitEl.textContent = ' ' + unit;
    
    card.appendChild(title);
    card.appendChild(val);
    val.appendChild(unitEl);
    
    return card;
}

function createPanel(sectionKey, sectionData, colorIndex) {
    const panel = document.createElement('div');
    panel.className = `panel panel-color-${colorIndex % 8}`;

    const meta = namesData[sectionKey] || {};
    const name = meta.name || sectionKey.toUpperCase();
    const desc = meta.description || '';

    const header = document.createElement('div');
    header.className = 'panel-header';
    header.textContent = name;
    if (desc) header.title = desc;

    const cards = document.createElement('div');
    cards.className = 'panel-cards';

    Object.keys(sectionData).forEach(key => {
        cards.appendChild(createCard(key, sectionData[key], sectionKey));
    });

    panel.appendChild(header);
    panel.appendChild(cards);

    return panel;
}

function renderDashboard(data) {
    const dashboard = document.getElementById('dashboard');
    dashboard.innerHTML = '';

    let colorIndex = 0;
    Object.keys(data).forEach(section => {
        if (typeof data[section] === 'object' && data[section] !== null) {
            dashboard.appendChild(createPanel(section, data[section], colorIndex));
            colorIndex++;
        }
    });
}

function updateFooter() {
    const now = new Date().toLocaleTimeString();
    document.getElementById('lastUpdate').textContent = `Last update: ${now}`;
}

async function init() {
    await fetchNames();
    await fetchStatus();
    
    // Set up update intervals
    setInterval(fetchStatus, pollInterval);
}

window.addEventListener('DOMContentLoaded', init);
