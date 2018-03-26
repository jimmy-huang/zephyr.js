// Copyright (c) 2018, Intel Corporation.

// Sample mosquitto client that publishes messages to a mosquitto broker

// To run it on the FRDM-K64F, you'll need to connect via ethernet th your
// host machine (e.g. Linux)

// First, run the Mosquitto server on the linux
// sudo mosquitto -v -p 1883

// Then on a separate terminal, you can start the mosquitto subscriber client:
// mosquitto_sub -t sensors

// Make sure the FRDM-K64F is on the same interface, eg. add appropriate routes
// ip route add 192.168.1/24 dev eno1

var mqtt = require('mqtt');
var client  = mqtt.connect('mqtt://192.168.1.101:1883');

console.log("Started MQTT client publisher");

client.on('connect', function () {
    client.subscribe('presence');
    client.publish('presence', 'Hello mqtt');
});

client.on('message', function (topic, message) {
    // message is Buffer
    console.log(message.toString());
    client.end();
});
