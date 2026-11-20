async function getErrors() {
    const response = await fetch('/faults');
    if (!response.ok) return;
    const errors = await response.json();
    if (errors.error) return;

    const box = document.getElementById('errors');
    if (errors.length === 0) {
        box.textContent = 'No faults';     // so the box isn't just blank
        return;
    }
    box.textContent = '';
    for (let i = 0; i < errors.length; i++) {
        box.textContent += (errors[i].error + '\n');
    }
}

setInterval(() => {
    getErrors();
}, 1000);