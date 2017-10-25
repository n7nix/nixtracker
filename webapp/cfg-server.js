"use strict";

/* Optional:
 * You will see the process.title name in 'ps', 'top', etc.
 */
process.title = 'cfg-server';
var DEFAULT_CFGS_INI = '/home/gunn/tmp/node_cfg.ini';
var DEFAULT_CONNECT_INI = '/home/gunn/tmp/node_connect.ini';

// Install node modules globally npm -g install <module_name>
var global_module_dir='/usr/local/lib/node_modules/';

var fs = require('fs');
var mod_ctype = require(global_module_dir + 'ctype');
var iniparser = require(global_module_dir + 'iniparser');

var HTMLPORT        // HTML server
var webSocketPort;  // Web socket server

var http = require('http');
var events = require("events");

var msg_emitter = new events.EventEmitter();

var inicfgdata;

/**
 * HTTP server
 **/
var webSocketServer = require(global_module_dir + 'websocket').server;


// parse command line for an ini file name
var ini_cfgs_file=DEFAULT_CFGS_INI;
var ini_connect_file=DEFAULT_CONNECT_INI;

if( process.argv[2] !== undefined) {
	ini_connect_file=process.argv[2];
}

fs.exists(ini_connect_file, function(exists) {
	if (!exists) {
		console.log('Can not open ' + ini_file + ' exiting');
		return;
	}
});

function parseINIString(data){
	var regex = {
		section: /^\s*\[\s*([^\]]*)\s*\]\s*$/,
			 param: /^\s*([\w\.\-\_]+)\s*=\s*(.*?)\s*$/,
			 comment: /^\s*;.*$/
	};
	var value = {};
	var lines = data.split(/\r\n|\r|\n/);
	var section = null;
	lines.forEach(function(line){
		if(regex.comment.test(line)){
			return;
		}else if(regex.param.test(line)){
			var match = line.match(regex.param);
			if(section){
				value[section][match[1]] = match[2];
			}else{
				value[match[1]] = match[2];
			}
		}else if(regex.section.test(line)){
			var match = line.match(regex.section);
			value[match[1]] = {};
			section = match[1];
		}else if(line.length == 0 && section){
			section = null;
		};
	});
	return value;
}

/**
 * Get config from ini file
 **/
iniparser.parse(ini_connect_file, function(err, data) {
	if (err) {
		console.log('Error: %s, failed to read %s', err.code, err.path);
		return;
	}
	/*
	 * Dump all the ini file parameters
	 */
	var util = require('util');
	var ds = util.inspect(data);
	console.log('iniparse test, ui_net: ' + ds);

	inicfgdata = JSON.stringify(data);

	webSocketPort = data.ui_net.websock_port;

	if( data.ui_net.websock_port === 'undefined' ) {
		console.log('ini parse error: No web socket port defined, exiting');
		return;
	}

	HTMLPORT = data.ui_net.html_port;
	if( data.ui_net.html_port === 'undefined' ) {
		console.log('ini parse error: No html port defined, exiting');
		return;
	}

	console.log("websocket port set to: " + webSocketPort);

	//	var jsonini = JSON.parse(ds);
	//	console.log("Session jsonini: %j", jsonini);
	var jsondata = JSON.parse(JSON.stringify(data));
	var jsonds = JSON.parse(JSON.stringify(data));


	var json_input = inicfgdata
	// try to parse JSON
	try {
		var json = JSON.parse(json_input);
	} catch (e) {
		console.log('This doesn\'t look like valid JSON: '+ json.type + " data: " + json.data);
	}

	console.log('json test: ' + inicfgdata);
//	console.log('ds json stringify: ' + JSON.stringify(ds));
//	console.log("Session ds: %j", ds);

	/**
	 * HTTP server
	 **/
	var server = http.createServer(function(request, response) {
		if (err) {
			console.log('Error creating HTTP server: %s', err.code);
			return;
		}
		// Not important for us. We're writing WebSocket server, not HTTP server
		console.log("HTTP Server created");
	});

	server.listen(webSocketPort, function() {
		console.log((new Date()) + " WebSocket Server is listening on port " + webSocketPort);
	});


	/**
	 * ===================== WebSocket server ========================
	 */
	var wsServer = new webSocketServer({
		/* WebSocket server is tied to an HTTP server.
		 * WebSocket request is just an enhanced HTTP request.
		 * For more info http://tools.ietf.org/html/rfc6455#page-6
		 */
		httpServer: server
	});

	// This callback function is called every time someone
	// tries to connect to the WebSocket server
	wsServer.on('request', function(request) {

		/* accept connection
		 * client is connecting from your website
		 * (http://en.wikipedia.org/wiki/Same_origin_policy)
		 */
		var connection = request.accept(null, request.origin);

		console.log((new Date()) + ' Connection from ' + request.origin);

		/* This code is not used
		 * No input from spy page is expected
		 */
		connection.on('message', function(message) {


			if (message.type === 'utf8') { // accept only text

				/* get data sent from users web page */
				var frontendmsg = JSON.parse(message.utf8Data);

				console.log("connection: received message type: " + frontendmsg.type);

				if(frontendmsg.type === "getconfig") {

					var json = JSON.stringify(inicfgdata);
					var sendData = JSON.stringify({ type:'cfginit', data: inicfgdata });

					connection.sendUTF(sendData);
					console.log('Sending GET config to webpage: length =' + sendData.length + ' data: ' + sendData );


				} else 	if(frontendmsg.type === "cfgready") {
					var json = JSON.stringify({ type:'putconfig', data: frontendmsg.data });

					var sendData = JSON.stringify({ type:'cfg', data: inicfgdata });

					connection.sendUTF(sendData);
					console.log('Sending ini config to web page: length =' + sendData.length + ' data: ' + sendData);


//					msg_emitter.emit("cfg", JSON.stringify(data) );
				} else {
					console.log('Unhandled message type from client: ' + frontendmsg.type);
				}
			}
		});

		// user disconnected
		connection.on('close', function(connection) {
			console.log((new Date()) + " Peer "
				    + connection.remoteAddress + " disconnected.");
		});
	});

	/**
	 * ===================== HTML server ========================
	 */

	var connect = require(global_module_dir + 'connect');
	connect.createServer(
			     connect.static(__dirname),
			     function(req, res){
		res.setHeader('Content-Type', 'text/html');
		res.end('You need a path, try /cfg.html\n');
	}
	).listen(HTMLPORT);

}); /* End brace for ini parser */
