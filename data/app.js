let namesData = {};
let pollInterval = 15;
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
        
        if (data.inverter && data.inverter.read_interval_ms) {
            const newInterval = data.inverter.read_interval_ms / 1000; // seconds
            if (newInterval !== pollInterval) {
                pollInterval = newInterval;
                schedulePoll();
            }
        }
        
        renderDashboard(data);
        updateFooter();
    } catch (e) {
        console.error('Failed to fetch status:', e);
        document.getElementById('lastUpdate').textContent = 'Connection error';
    }
}

function formatValue(val) {
    if (val === null || val === undefined || isNaN(val)) return '--';
    if (Number.isInteger(val)) return val;
    return parseFloat(val).toFixed(1);
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
    val.textContent = formatValue(value);
    
    const unitEl = document.createElement('div');
    unitEl.className = 'card-unit';
    unitEl.textContent = unit;
    
    card.appendChild(title);
    card.appendChild(val);
    card.appendChild(unitEl);
    
    return card;
}

function createPanel(sectionKey, sectionData) {
    const panel = document.createElement('div');
    panel.className = 'panel';
    
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
    
    Object.keys(data).forEach(section => {
        if (typeof data[section] === 'object' && data[section] !== null) {
            dashboard.appendChild(createPanel(section, data[section]));
        }
    });
}

function updateFooter() {
    const now = new Date().toLocaleTimeString();
    document.getElementById('lastUpdate').textContent = `Last update: ${now}`;
}

function schedulePoll() {
    if (timeoutId) clearTimeout(timeoutId);
    timeoutId = setTimeout(() => {
        fetchStatus();
    }, pollInterval);
}

async function init() {
    await fetchNames();
    await fetchStatus();
}

init();