/*
    sd.js

    graph logic for the SD card page: shows a recording uploaded from the car's SD card.
    Reads from /sd/sensors and /sd/data/<id>, which only look at the uploaded file,
    never the live database.
*/

const sdChart = new Chart("sdGraph", {
    type: "line",
    data: {
        labels: [],
        datasets: [{
            label: "",
            data: [],
            borderColor: "rgba(0,0,255,0.6)",
            backgroundColor: "rgba(0,0,0,0)",   // no fill under the line
            borderWidth: 1
        }]
    },
    options: {
        title: { display: true, text: "Upload a file, then pick a sensor" },
        legend: { display: false },
        animation: { duration: 0 },           // no animation: recordings can be big
        elements: { point: { radius: 0 } },   // no dot on every point
        scales: {
            xAxes: [{ scaleLabel: { display: true, labelString: "Seconds from start of recording" } }]
        }
    }
}); 

async function loadSdSensors() {
    const dropdown = document.getElementById("sd-sensor");
    const response = await fetch("/sd/sensors");
    if (!response.ok) return;           
    const sensors = await response.json();
    // clear old options, keeping the first "Select a sensor" one
    dropdown.length = 1;
    dropdown.selectedIndex = 0;
    for (const s of sensors) {
        const option = document.createElement("option");
        option.value = s.sensor_id;
        option.text = s.name + " (" + s.unit + ")";
        dropdown.appendChild(option);
    }
}

async function loadSdData(sensorId) {
    const response = await fetch("/sd/data/" + sensorId);
    if (!response.ok) return;
    const rows = await response.json();
    if (rows.length === 0) return;
    const firstTime = rows[0].timestamp;
    sdChart.data.labels = rows.map(r => (r.timestamp - firstTime).toFixed(2));
    sdChart.data.datasets[0].data = rows.map(r => r.data);
    sdChart.options.title.text = rows[0].name + " (" + rows[0].unit + "): " + rows.length + " readings";
    sdChart.update();
}

document.getElementById("sd-sensor").addEventListener("change", function () {
    loadSdData(this.value);
});

// if a file was already uploaded earlier, fill the dropdown right away
loadSdSensors();

