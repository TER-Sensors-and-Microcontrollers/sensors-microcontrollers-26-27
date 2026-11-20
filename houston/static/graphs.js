/*
    graph.js
    
    real-time graph logic
*/

// initial starting values for the 3 graphs, subject to change
var selectedValue1 = 1610;
var selectedValue2 = 1651;
var selectedValue3 = 1670;

var currentX = "motor temp"
var currentY = "battery"

// get absolute start time from browser cache if it exists
var start = sessionStorage.getItem("startTime");
if (!start) {
    // Deserialize the JSON string back into a JavaScript object
    start = Date.now();
    sessionStorage.setItem("startTime", JSON.stringify(start));
}

/*
    updateGraph

    Adds the most recently grabbed datapoint from a preexisting sensor,
    and adds it to its corresponding chart.
    input(s): sensor id, variable name of graph
    notes: 
        - saves chart to cache based on CANVAS ID (see chartjs documentation),
        - is async
        - sensor(s) with given id must exist within database
*/

// --- Sliding window settings ---
// true  = each graph only keeps its newest MAX_POINTS points (old ones drop off the left)
// false = graphs keep every point forever
const SLIDING_WINDOW = true;
const MAX_POINTS = 100;

// each graph's bookmark: the reading_id of the newest point it has drawn
let lastId = JSON.parse(sessionStorage.getItem("lastId") || '{"graph1":0,"graph2":0,"graph3":0}');

/*
    trimGraph

    If the sliding window is on, removes the oldest points so the graph
    holds at most MAX_POINTS. If it's off, does nothing.
*/
function trimGraph(g) {
    if (!SLIDING_WINDOW) return;
    const extra = g.data.labels.length - MAX_POINTS;
    if (extra > 0) {
        g.data.labels.splice(0, extra);
        g.data.datasets[0].data.splice(0, extra);
    }
}


async function updateGraph(sid, g)
{   
    try{
        const since = lastId[g.canvas.id] || 0;
        const response = await fetch('/get_since/' + sid + '/' + since);
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }

        const rows = await response.json();
        if (rows.length === 0) return;      // nothing new: draw nothing, no repeated point

        for (const r of rows) {
            g.data.labels.push((r.timestamp - (start / 1000)).toFixed(2));
            g.data.datasets[0].data.push(r.data);
        }
        lastId[g.canvas.id] = rows[rows.length - 1].reading_id;
        sessionStorage.setItem("lastId", JSON.stringify(lastId));
        trimGraph(g);

        g.options.title.text = rows[0].name + " (" + rows[0].unit + ") Over Time";
        g.update(0);                         // one redraw for the whole batch, no animation

        saveToSessionStorage(g.canvas.id, {
            labels: g.data.labels,
            datasets: g.data.datasets.map(ds => ({
                backgroundColor: ds.backgroundColor,
                borderColor: ds.borderColor,
                data: ds.data
            }))
        });
    }
    catch (error) {
        console.error("Error fetching or parsing data:", error);
    }
}
        


async function clearGraph(g) {
    g.data.labels = [];
    g.data.datasets[0].data = [];
    g.update();
    saveToSessionStorage(g.canvas.id,
    {
        labels: [],
        datasets: [{
            label: "",
            backgroundColor:"rgba(255,255,255,1.0)",
            borderColor: "rgba(0,0,255,0.1)",
            data: []
            }]
    });
    console.log("graph " + g + " cleared...");
}
/*
    saveToSessionStorage

    Given chart name and cachable chart data, saves chart data in user's browser cache
    input(s): name/key to save chart data field as, chart data field to be cached
*/
function saveToSessionStorage(name, data) {
    // sessionStorage can only store strings, so we must serialize the data to JSON
    sessionStorage.setItem(name, JSON.stringify(data));
}
/*
    loadFromSessionStorage

    Given name of chart to load, return chart data from user's browser cache, 
    if there exists any. Else, return a fresh instance of chart data
    input(s): name under which chart data was saved as (in saveToSessionStorage)
*/
function loadFromSessionStorage(name) {
    // filler values to put into fresh chart
    const xValues = [];
    const yValues = [];
    const storedData = sessionStorage.getItem(name);
    if (storedData) {
        // Deserialize the JSON string back into a JavaScript object
        return JSON.parse(storedData);
    }
    // Return a default data structure if nothing is saved yet
    return {
        labels: xValues,
        datasets: [{
            label: name,
            backgroundColor:"rgba(255,255,255,1.0)",
            borderColor: "rgba(0,0,255,0.1)",
            data: yValues
            }]
    };
}

const LINE_STYLE = {
    borderColor: "rgba(0,0,255,0.6)",
    backgroundColor: "rgba(0,0,0,0)",
    borderWidth: 1
};

function liveGraphOptions(title) {
    return {
    title: { display: true, text: title },
        legend: { display: false },
        animation: { duration: 0 },
        elements: { point: { radius: 0 } },
        scales: {
            xAxes: [{ scaleLabel: { display: true, labelString: "Seconds since dashboard opened" } }]
        }
    };
}

function styledData(name) {
    const data = loadFromSessionStorage(name);
    Object.assign(data.datasets[0], LINE_STYLE);
    return data
}

const g1 = new Chart("graph1", {
    type: "line",
    data: styledData("graph1"),
    options: liveGraphOptions('Sensor 1 Over Time')
})

const g2 = new Chart("graph2", {
    type: "line",
    data: styledData("graph2"),
    options: liveGraphOptions('Sensor 2 Over Time')
})

const g3 = new Chart("graph3", {
    type: "line",
    data: styledData("graph3"),
    options: liveGraphOptions('Sensor 3 Over Time')
})

// scatter plot: each point is one (X, Y) pair from /pair/<x>/<y>
const scatter = new Chart("scatter", {
    type: "scatter",
    data: {
        datasets: [{
            data: [],
            backgroundColor: "rgba(0,0,255,0.4)",
            pointRadius: 2
        }]
    },
    options: {
        title: {display: true, text: "Pick sensors for the X and Y axes"},
        legend: {display: false},
        animation: {duration: 0},
        scales: {
            xAxes: [{scaleLabel: {display: true, labelString: "X"}}],
            yAxes: [{scaleLabel: {display: true, labelString: "Y"}}]
        }
    }
});

 document.addEventListener('DOMContentLoaded', function() {
    var dropdown = document.getElementById('g1');
                dropdown.addEventListener('change', function() 
                {
                    clearGraph(g1);
                    selectedValue1 = dropdown.value;
                    get_new_data(selectedValue1, g1);
                });
 });

 document.addEventListener('DOMContentLoaded', function() {
    var dropdown = document.getElementById('g2');
                dropdown.addEventListener('change', function() 
                {
                    clearGraph(g2);
                    selectedValue2 = dropdown.value;
                    get_new_data(selectedValue2, g2);
                });
 });

 document.addEventListener('DOMContentLoaded', function() {
    var dropdown = document.getElementById('g3');
                dropdown.addEventListener('change', function() 
                {
                    clearGraph(g3);
                    selectedValue3 = dropdown.value;
                    get_new_data(selectedValue3, g3);
                });
 });

// sensors picked for the scatter plot (null until picked)
let scatterXId = null;
let scatterYId = null;

document.addEventListener('DOMContentLoaded', function() {
    document.getElementById("scatterX").addEventListener('change', function() {
        scatterXId = this.value;
        updateScatter();
    });
    document.getElementById("scatterY").addEventListener('change', function () {
        scatterYId = this.value;
        updateScatter();
    });
});

function selectedName(dropdownId) {
    const d = document.getElementById(dropdownId);
    return d.options[d.selectedIndex].text;
}

async function updateScatter() {
    if (!scatterXId || !scatterYId) return;
    if (document.getElementById('sc').classList.contains('hidden')) return;
    try {
        const response = await fetch('/pair/' + scatterXId + '/' + scatterYId);
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        const points = await response.json();
        const xName = selectedName('scatterX');
        const yName = selectedName('scatterY');
        scatter.data.datasets[0].data = points;
        scatter.options.title.text = yName + " vs " + xName + " (" + points.length + " points)";
        scatter.options.scales.xAxes[0].scaleLabel.labelString = xName;
        scatter.options.scales.yAxes[0].scaleLabel.labelString = yName;
        scatter.update(0);
    }
    catch (error) {
        console.error("Error fetching or parsing scatter data:", error);
    }
}

//  get all up-to-date data of selected id
 async function get_new_data(selectedValue, g)
 {
    const new_data = await fetch('/get_all_data/' + selectedValue);
        if (!new_data.ok) {
        throw new Error(`HTTP error! status: ${new_data.status}`);
        }
    
    const all_data = await new_data.json();

    for (let r = 0; r < all_data.length; r++) {
        g.data.labels.push((all_data[r].timestamp - (start / 1000))).toFixed(2);
        g.data.datasets[0].data.push(all_data[r].data);
    }

    // bookmark the newest reading loaded, so updateGraph only fetches what comes after it
    if (all_data.length > 0 && g !== scatter) {
        lastId[g.canvas.id] = all_data[all_data.length - 1].reading_id;
        sessionStorage.setItem("lastId", JSON.stringify(lastId));
        trimGraph(g)
    }

    if (g === scatter)
        g.options.title.text = currentY + " vs " + currentX;
    else
        g.options.title.text = all_data[0].name + " Over Time";
    g.update();
    
    saveToSessionStorage(g.canvas.id, {
            labels: g.data.labels,
            datasets: g.data.datasets.map(ds => ({
                backgroundColor: ds.backgroundColor,
                borderColor: ds.borderColor,
                data: ds.data
            })),
            options: {
                title: {
                    display: true,
                    text: all_data[0].name + " (" + all_data[0].unit + ") Over Time",
                }
            }
        })


 }
setInterval(() => {
    updateGraph(selectedValue1, g1);
    updateGraph(selectedValue2, g2);
    updateGraph(selectedValue3, g3);
    updateScatter();
}, 500);
