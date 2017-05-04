// Copyright (c) 2017, Intel Corporation.

// Test code to use the Curie Pattern Matching Engine within the Intel Curie Compute
// on the Arduino 101/tinyTile, which uses the PME API to train and classify data
console.log("Curie Pattern Matching Engine save and restore...");

// import pme and fs module
var pme = require("pme");
var fs = require("fs");

var file = "neurons.txt";

var training1 = [ 10, 10, 10, 10 ];
var training2 = [ 20, 20, 20, 20 ];
var training3 = [ 30, 30, 30, 30 ];
var training4 = [ 40, 40, 40, 40 ];
var training5 = [ 50, 50, 50, 50 ];


function pme_stats() {
    console.log("");
    console.log("PME statistics");
    console.log("==============:");
    var mode = pme.getClassifierMode();
    if (mode === pme.RBF_MODE) {
        console.log("Classification mode: RBF_MODE");
    } else if (mode === pme.RBF_MODE) {
        console.log("Classification mode: KNN_MODE");
    }

    mode = pme.getDistanceMode();
    if (mode === pme.L1_DISTANCE) {
        console.log("Distance mode: L1_DISTANCE");
    } else if (mode === pme.LSUP_DISTANCE) {
        console.log("Distance mode: LSUP_DISTANCE");
    }

    for (var i = 1; i <= pme.getCommittedCount(); i++) {
        var neuron = pme.readNeuron(i);
        console.log("Neuron id=" + i +
                    " category=" + neuron.category +
                    " context=" + neuron.context +
                    " influence=" + neuron.influence +
                    " minInfluence=" + neuron.minInfluence);
    }
}

var fd1 = fs.openSync(file, "w");

pme.begin();
pme.learn(training1, 100);
pme.learn(training2, 200);
pme.learn(training3, 300);
pme.learn(training4, 400);
pme.learn(training5, 500);

console.log("Finished training");
pme_stats();

var neurons = pme.saveNeurons();
var jsonOut = JSON.stringify(neurons);
/*
[{
    "context": 1,
    "AIF": 887,
    "category": 2,
    "vector": [1, 2, 3]
},
{
    "context": 1,
    "AIF": 78,
    "category": 3,
    "vector": [10, 20, 30]
}]);
*/

console.log("Neurons in json " + jsonOut);
console.log("Saved Neurons to file " + file);

fs.writeFileSync(file, jsonOut);

pme.forget();
console.log("Cleared engine");
pme_stats();

var fd2  = fs.openSync(file, "r");
console.log("File opened");
var stats = fs.statSync(file);
console.log("stat.size " + stats.size);
var buf = new Buffer(stats.size);
console.log("allocated buffer " + stats.size);
fs.readSync(fd2, buf, 0, stats.size, 0);
console.log("File content " + buf.toString("ascii"));

console.log("Restored");
pme_stats();

fs.closeSync(fd2);
