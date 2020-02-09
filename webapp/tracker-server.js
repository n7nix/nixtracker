// http://ejohn.org/blog/ecmascript-5-strict-mode-json-and-more/
"use strict";

// Optional. You will see this name in eg. 'ps' or 'top' command
process.title = 'tracker-server';

var fs = require('fs');

// parse command line for an ini file name
var ini_file='./aprs.ini';
if( process.argv[2] !== undefined) {
        ini_file=process.argv[2];
}

fs.exists(ini_file, function(exists) {
        if (!exists) {
                console.log('Can not open ' + ini_file + ' exiting');
                return;
        }
});

// Install node modules globally npm -g install <module_name>
var global_module_dir='/usr/local/lib/node_modules/';

// Optional. You can see this name displayed in 'ps' or 'top' command
process.title = 'track-server';

var mod_ctype = require(global_module_dir + 'ctype');

// *** Network socket server
var net = require('net');
var NETHOST;
var NETPORT;
var UNIXPORT;

var HTMLPORT;
var webSocketPort;

/**
 * Get config from ini file
 **/
var iniparser = require(global_module_dir + 'iniparser');
iniparser.parse(ini_file, function(err, data) {
        if (err) {
                console.log('Error: %s, failed to read %s', err.code, err.path);
                return;
        }
        /*
         * Dump all the ini file parameters
         */
//        var util = require('util');
//        var ds = util.inspect(data);
//        console.log('iniparse test, ui_net: ' + ds);

        console.log("Check ini parameters websock: " +  data.ui_net.websock_port + " html port: " + data.ui_net.html_port);

        webSocketPort = data.ui_net.websock_port;
        if( data.ui_net.websock_port === undefined ) {
                console.log('ini parse error: No web socket port defined, exiting');
                return;
        }

        HTMLPORT = data.ui_net.html_port;
        if( data.ui_net.html_port === undefined ) {
                console.log('ini parse error: No html port defined, exiting');
                return;
        }

	NETHOST = data.ui_net.sock_hostname;
	NETPORT = data.ui_net.sock_port;

        if( data.ui_net.unix_socket != undefined ) {
                UNIXPORT = data.ui_net.unix_socket;
                NETPORT = UNIXPORT;
	}

	console.log("Connections: websocket port: " + webSocketPort + '  Net Host: ' + NETHOST + ' port: ' + NETPORT);

//var broadcastPort = webSocketPort + 1;
var broadcastPort = 43256;
var TCP_BUFFER_SIZE = Math.pow(2,16);


/**
 * Global variables
 */
// latest 100 messages
var history = [ ];
// list of currently connected clients (users)
var trkClients = [];

var netClients = [ ];
var wsClients = [ ];
var serverIPaddress='';
var connectionIDcounter = 0;


// websocket and http servers
var webSocketServer = require(global_module_dir + 'websocket').server;
var http = require('http');
var events = require("events");

var aprs_emitter = new events.EventEmitter();
var msg_emitter = new events.EventEmitter();

// Broadcast Server
var dgramServer = require('dgram');

// Enhance JSON method with better error handling
(function(){

	var parse = JSON.parse;

	JSON = {

		stringify: JSON.stringify,

	        validate: function(str){

			try{
				parse(str);
				return true;
			} catch(err){
				return err;
			   }

			   },

		parse: function(str){

			try{
				return parse(str);
			} catch(err){
				return undefined;
			}
		}
	}

})();

function showObjElements(myObj){
	for (var member in myObj){
		if (myObj.hasOwnProperty(member)){
			console.log(myObj[member]);
		}
	}
}

/**
 * Helper function for escaping input strings
 */
function htmlEntities(str) {
	return String(str).replace(/&/g, '&amp;').replace(/</g, '&lt;')
			.replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function send_aprs_display(message, orig) {

	/*
	 * Temporary Debug to catch any invalid json
	 */

	if(JSON.validate(message) !== true) {
		console.log('Caught invalid json string: ' + message );
		console.log('original string: ' + orig.toString('utf8') );
	}
	if(trkClients[0] !== undefined && trkClients.length !== undefined) {

		for (var i=0; i < trkClients.length; i++) {
//                        console.log('send_aprs_display: sending to ' +  i + ' len: '+ message.length);
			trkClients[i].sendUTF(message);
		}
	}
}

// Array with some colors, don't use the background color, currently orange
   var colors = [ 'red', 'green', 'blue', 'magenta', 'purple', 'plum', 'GreenYellow', 'DarkKhaki', 'Brown', 'SaddleBrown', 'SteelBlue' ];
// ... in random order
   colors.sort(function(a,b) { return Math.random() > 0.5; } );

if(UNIXPORT != undefined) {
        fs.unlink(UNIXPORT, function(err) {
                if (err) {
                        console.log('Will create socket:' + UNIXPORT );
                } else {
                        console.log('successfully deleted:' + UNIXPORT );
                };
        });
}

/**
 *
 * HTTP server for websocket
 **/
var server = http.createServer(function(request, response) {
        // Not important for us. We're writing WebSocket server, not HTTP server
	console.log((new Date()) + ' http server Received request for ' + request.url);
	response.writeHead(404);
	response.end();
});
server.listen(webSocketPort, function() {
	console.log((new Date()) + " Web Socket Server is listening on port " + webSocketPort);
});

/**
 * ===================== WebSocket server ========================
 */
var wsServer = new webSocketServer({
    // WebSocket server is tied to a HTTP server. WebSocket request is just
    // an enhanced HTTP request. For more info http://tools.ietf.org/html/rfc6455#page-6
	httpServer: server,
	autoAcceptConnections: false
});

function originIsAllowed(origin) {
	return true;
}

wsClients.push(wsServer);

wsServer.on('error', function (exc) {
	console.log("websocket server: ignoring exception: " + exc);
});

// This callback function is called every time someone
// tries to connect to the WebSocket server
wsServer.on('request', function(request) {

	var userName = false;
	var destName = false;
	var userColor = false;

	if (!originIsAllowed(request.origin)) {
		request.reject();
		console.log((new Date()) + ' Connection from origin ' + request.origin + ' rejected.');
		return;
	}
	console.log((new Date()) + ' Connection from origin ' + request.origin);

	// accept connection - you should check 'request.origin' to make sure that
	// client is connecting from your website
	// (http://en.wikipedia.org/wiki/Same_origin_policy)
	var connection = request.accept(null, request.origin);
	// we need to know client index to remove them on 'close' event
	connection.id = connectionIDcounter;
	var index = trkClients.push(connection) - 1;
	connectionIDcounter++;

	// send back chat history
	if (history.length > 0) {
		connection.sendUTF(JSON.stringify( { type: 'history', data: history} ));
	}

//	showObjElements(request);
	console.log('OPENING connection: id: ' + connection.id + ', index: ' + index + ', ip: ' + connection.remoteAddress + ', socket ip: ' + 	connection.socket.remoteAddress + ', connections open: ' + trkClients.length);

	// user sent some message
        connection.on('message', function(message) {
                if (message.type === 'utf8') { // accept only text

                        /* get data sent from users web page */
                        var frontendmsg = JSON.parse(message.utf8Data);

                        if(frontendmsg.type === "sysctrl") {
                                var json = JSON.stringify({ type:'sysctrl', data: frontendmsg.data});
                                console.log('Sending sysctrl to CPU: length =' + json.length + ' data: ' + frontendmsg.data);
                                msg_emitter.emit("aprs_sock", json);

                        } else if(frontendmsg.type === "setconfig") {
                                var json = JSON.stringify({ type:'setconfig', data: frontendmsg.data});
                                console.log('Sending SET config to radio: length =' + json.length + ' data: ' + frontendmsg.data);
                                msg_emitter.emit("aprs_cfg", json);

                        } else if(frontendmsg.type === "getconfig") {
                                var json = JSON.stringify({ type:'getconfig', data: frontendmsg.data});
                                console.log('Sending GET config to radio: length =' + json.length + ' data: ' + frontendmsg.data);
                                msg_emitter.emit("aprs_sock", json);

                        } else if(frontendmsg.type === "callsign") {

                                // remember user name
                                userName = htmlEntities(frontendmsg.data);
                                // get random color and send it back to the user
                                //userColor = colors.shift();
                                userColor='Maroon';
                                connection.sendUTF(JSON.stringify({ type:'color', data: userColor }));
                                console.log((new Date()) + ' User is known as: ' + userName
                                            + ' with ' + userColor + ' color.');

                        } else if(frontendmsg.type === "sendto") {
                                // save destination name
                                destName = htmlEntities(frontendmsg.data);
                                connection.sendUTF(JSON.stringify({ type:'sendto', data: destName }));
                                console.log((new Date()) + ' Destination: ' + destName);

                        } else if(frontendmsg.type === "message") {

                                // log and broadcast the message
                                console.log((new Date()) + ' Received Message from '
                                            + userName + ': ' + frontendmsg.data);

                                // we want to keep history of all sent messages
                                var obj = {
                                        time: (new Date()).getTime(),
                                              text: htmlEntities(frontendmsg.data),
                                              from: userName,
                                              sendto: destName,
                                              color: userColor
                                };

                                var histelements = history.length;
                                history.push(obj);
                                // keep a fixed length quueue
                                history = history.slice(-5);
                                console.log('history before: ' + histelements + ' after: ' + history.length);

                                // broadcast message to all connected clients
                                var json = JSON.stringify({ type:'message', data: obj });
                                for (var i=0; i < trkClients.length; i++) {
                                        trkClients[i].sendUTF(json);
                                }

                                connection.sendUTF(JSON.stringify({ type:'color', data: userColor }));

                                console.log('Sending message to radio: length =' + json.length);
                                /* Send message to radio to be transmitted via aprs */
                                delete obj.color;
                                var json = JSON.stringify({ type:'message', data: obj });
                                msg_emitter.emit("aprs_msg", json);
                                destName = false;

                        // Handle default
                        } else {
                                console.log('Unhandled message type from client: ' + frontendmsg.type);
                        }
                }
                });  //connection on message

	// user disconnected
	connection.on('close', function(reasonCode, description) {

		var i = 0;

		while( i < trkClients.length) {

			if(trkClients[i].id == connection.id) {
				// remove user from the list of connected clients
				trkClients.splice(i, 1);
				console.log((new Date()) + ' Peer: ' + connection.remoteAddress + ' CLOSING Connection id: ' + connection.id  + ', remaining: ' + trkClients.length + ', reason: ' + description);
				break;
			}
			i++;
		}

		// push back user's color to be reused by another user
		colors.push(userColor);

	}); //connection on close
	connection.on('error', function (exc) {
		console.log("websocket connection: ignoring exception: " + exc);
	});

}); // wsServer on

/**
 * ===================== network Socket server ========================
 *
 * Create a server instance, and chain the listen function to it
 * The function passed to net.createServer() becomes the event handler for the 'connection' event
 * The sock object the callback function receives UNIQUE for each connection
 */
net.createServer(function(sock) {

	var jchunk = "";

	// We have a connection - a socket object is assigned to the connection automatically
	console.log('CONNECTED: ' + sock.remoteAddress +':'+ sock.remotePort);

	// To buffer tcp data see:
	// http://stackoverflow.com/questions/7034537/nodejs-what-is-the-proper-way-to-handling-tcp-socket-streams-which-delimiter
	var buf = new Buffer(TCP_BUFFER_SIZE);  //new buffer with size 2^16

	//	sock.emit(news, {hello: world});
	netClients.push(sock);

	if(trkClients[0] !== undefined && trkClients[0].length !== undefined) {
		console.log("net0: Chat Client length: " +  trkClients[0].length );
	} else {
		console.log("net0: NO Chat Client");
	}

	// 'data' event handler for this instance of socket
	sock.on('data', function(data) {
		jchunk += data.toString('utf8');
		var origdata = data; // save the starting string for debug

		// Check for TCP stream combining JSON objects
		var d_index = jchunk.search("}{");
		if(d_index == -1) { // No combined JSON objects
			// Verify json object is complete
			if(JSON.validate(jchunk) === true) {
				send_aprs_display(jchunk, origdata);
				jchunk = "";
			}
		} else {
			// Get the concatenated JSON objects?
			while (d_index != -1) {
				var jmsg = jchunk.slice(0, d_index+1);
				jchunk = jchunk.slice(d_index+1);

				send_aprs_display(jmsg, origdata);
				d_index = jchunk.search("}{");
			}
			// Verify last json object in concatenated
			// stream is complete.
			if(JSON.validate(jchunk) === true) {
				send_aprs_display(jchunk, origdata);
				jchunk = "";

			}
		}
	});

	// Add a 'close' event handler to this instance of socket
	sock.on('close', function(data) {
		console.log('Unix socket connection closed at: ' + (new Date()) + 'socket address: ' + sock.remoteAddress +' '+ sock.remotePort);
	});

	msg_emitter.on("aprs_msg", function(message) {
		console.log('MSG: ' + message);
		sock.write(message);
	});

	msg_emitter.on("aprs_cfg", function(message) {
		console.log('CFG: ' + message);
		sock.write(message);
	});

	msg_emitter.on("aprs_sock", function(message) {
		console.log('aprs_sock: ' + message);
		sock.write(message);
	});

}).listen(NETPORT, NETHOST); /* create server */

console.log((new Date()) + 'UI Socket Server listening on ' + NETHOST +':'+ NETPORT);

/**
 * ===================== HTML server ========================
 */

var connect = require(global_module_dir + 'connect');

/* For connect@2.x ONLY, Does not work with connect@3.x
connect.createServer(
		connect.static(__dirname),
		function(req, res){
	res.setHeader('Content-Type', 'text/html');
	res.end('You need a path, try /tracker.html\n');
}
).listen(HTMLPORT);
*/

var serveStatic = require(global_module_dir + 'serve-static');
var finalhandler = require(global_module_dir + 'finalhandler');

var app = connect();
var serve = serveStatic(__dirname, {'index': ['tracker.html']})
	    // Create server
	    var server = http.createServer(function(req, res) {
		    var done = finalhandler(req, res)
			       serve(req, res, done)
	    }).listen(HTMLPORT);

	    console.log('HTML Server listening on port: ' + HTMLPORT);
	});
