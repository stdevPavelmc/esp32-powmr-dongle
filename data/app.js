let namesData = {};
let pollInterval = 5; // 5 seconds
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

    // Get unit information from namesData
    const sectionMeta = namesData[sectionKey] || {};
    const fieldMeta = sectionMeta[key] || {};
    const unit = fieldMeta.unit || '';

    // Handle numeric values with ISU auto resizing
    if (typeof val === 'number' && !isTimeUnit(unit)) {
        return formatISUValue(val, unit);
    }

    // Handle time values with special formatting
    if (typeof val === 'number' && isTimeUnit(unit)) {
        return formatTimeValue(val, unit);
    }

    if (Number.isInteger(val) || typeof val === 'number' && val % 1 !== 0)
        return parseFloat(val).toFixed(1);

    // final resort, return the value
    return val;
}

function isTimeUnit(unit) {
    const timeUnits = ['min', 'mins', 's', 'secs', 'seconds', 'ms', 'milliseconds', 'h', 'hours', 'd', 'days'];
    return timeUnits.includes(unit.toLowerCase());
}

function formatISUValue(value, unit) {
    if (!unit || value === 0) {
        return value.toFixed(1);
    }

    const absValue = Math.abs(value);
    
    // ISU prefixes and their multipliers
    const prefixes = [
        { symbol: 'T', multiplier: 1e12 },
        { symbol: 'G', multiplier: 1e9 },
        { symbol: 'M', multiplier: 1e6 },
        { symbol: 'k', multiplier: 1e3 },
        { symbol: '', multiplier: 1 },
        { symbol: 'm', multiplier: 1e-3 },
        { symbol: 'μ', multiplier: 1e-6 },
        { symbol: 'n', multiplier: 1e-9 }
    ];

    // Find the appropriate prefix using 1.1 rule
    let selectedPrefix = prefixes[4]; // Default to no prefix
    for (let i = 0; i < prefixes.length; i++) {
        const threshold = i < 4 ? 1.1 * prefixes[i].multiplier : 0.91 * prefixes[i].multiplier;
        if (absValue >= threshold) {
            selectedPrefix = prefixes[i];
            break;
        }
    }

    const scaledValue = value / selectedPrefix.multiplier;
    const formattedValue = selectedPrefix.symbol ? scaledValue.toFixed(2) : scaledValue.toFixed(1);
    
    return `${formattedValue} ${selectedPrefix.symbol}${unit}`;
}

function formatTimeValue(value, unit) {
    const unitLower = unit.toLowerCase();
    
    // Handle milliseconds to seconds, minutes:seconds
    if (unitLower === 'ms' || unitLower === 'milliseconds') {
        if (value >= 60*1000) {
            const minutes = Math.floor(value / (60*1000));
            const seconds = (value % (60*1000)) / 1000;
            return `${minutes}m:${seconds.toFixed(1)}s`;
        }
        if (value >= 1000) {
            return `${(value / 1000).toFixed(2)} s`;
        }
        return `${value.toFixed(1)} ms`;
    }
    
    // Handle seconds to minutes:seconds, hours:minutes, days:hours:minutes
    if (unitLower === 's' || unitLower === 'secs' || unitLower === 'seconds') {
        if (value >= 60*60*24) {
            const days = Math.floor(value / (60*60*24));
            const hours = Math.floor((value % (60*60*24)) / (60*60));
            const minutes = Math.floor((value % (60*60)) / 60);
            return `${days}d:${hours}h:${minutes}m`;
        }
        if (value >= 60*60) {
            const hours = Math.floor(value / (60*60));
            const minutes = Math.floor((value % (60*60)) / 60);
            return `${hours}h:${minutes}m`;
        }
        if (value >= 60) {
            const minutes = Math.floor(value / 60);
            const seconds = Math.floor(value % 60);
            return `${minutes}m:${seconds}s`;
        }
        return `${value.toFixed(1)} s`;
    }
    
    // Handle minutes to hours:minutes or days:hours:minutes
    if (unitLower === 'min' || unitLower === 'mins') {
        if (value < 60) {
            return `${value.toFixed(1)} min`;
        }
        if (value < 60*24) {
            const hours = Math.floor(value / 60);
            const minutes = Math.floor(value % 60);
            return `${hours}h:${minutes}m`;
        }
        const days = Math.floor(value / (60*24));
        const hours = Math.floor((value % (60*24)) / 60);
        const minutes = Math.floor(value % 60);
        return `${days}d:${hours}h:${minutes}m`;
    }
    
    // Handle hours to days:hours
    if (unitLower === 'h' || unitLower === 'hours') {
        if (value >= 24) {
            const days = Math.floor(value / 24);
            const hours = Math.floor(value % 24);
            return `${days}d:${hours}h`;
        }
        return `${value.toFixed(1)} h`;
    }
    
    // Default: return value with unit
    return `${value.toFixed(1)} ${unit}`;
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
    
    // Format the value and check if unit is already included
    const formattedValue = formatValue(value, key, sectionKey);
    val.textContent = formattedValue;
    
    // Only add separate unit element if unit is not already in the formatted value
    if (unit && !formattedValue.includes(unit) && !isTimeUnit(unit)) {
        const unitEl = document.createElement('span');
        unitEl.className = 'card-unit';
        unitEl.textContent = ' ' + unit;
        val.appendChild(unitEl);
    }
    
    card.appendChild(title);
    card.appendChild(val);
    
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
    
    // Set up update intervals, unit is in seconds with decimals
    setInterval(fetchStatus, pollInterval * 1000);
}

window.addEventListener('DOMContentLoaded', init);
