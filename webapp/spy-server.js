// http://ejohn.org/blog/ecmascript-5-strict-mode-json-and-more/
"use strict";

/* Optional:
 * You will see the process.title name in 'ps', 'top', etc.
 */
process.title = 'spy-server';

// Install node modules globally npm -g install <module_name>
var global_module_dir='/usr/local/lib/node_modules/';

/**
 * Global variables
 */
// list of currently connected clients (users)
var spyClients = [];

// websocket and http servers
var webSocketServer = require(global_module_dir + 'websocket').server;
var http = require('http');
var events = require("events");
var fs = require('fs');
var mod_ctype = require(global_module_dir + 'ctype');
var iniparser = require(global_module_dir + 'iniparser');

// *** Network socket server
var net = require('net');
var NETHOST;
var NETPORT;
var UNIXPORT;

var HTMLPORT        // HTML server
var webSocketPort;  // Web socket server

/* Keep a history of messages */
var spy_totMsg = 0;
var spy_maxMsgStore = 25;
var spy_Message = [];

/* Refresh browser with history of stored messages */
function refreshBrowser(clientIndex)
{
	var msgRefreshCnt = (spy_totMsg < spy_maxMsgStore ? spy_totMsg : spy_maxMsgStore);

	for(var i=0; i < msgRefreshCnt; i++) {
		spyClients[clientIndex].sendUTF(spy_Message[i]);
	}
}

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

/**
 * Get config from ini file
 **/
iniparser.parse(ini_file, function(err, data) {
        if (err) {
                console.log('Error: %s, failed to read %s', err.code, err.path);
                return;
	}
	console.log('Using ini file: ' + ini_file);
        /*
         * Uncomment next 3 lines to dump all the ini file parameters
         */
//        var util = require('util');
//        var ds = util.inspect(data);
//	  console.log('iniparse test, ui_net: ' + ds);

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

	NETHOST = data.ui_net.sock_hostname;
	NETPORT = data.ui_net.sock_port;

	if( data.ui_net.unix_socket != undefined ) {
                UNIXPORT = data.ui_net.unix_socket;
                NETPORT = UNIXPORT;
        }

	console.log("Connections: websocket port: " + webSocketPort + '  Net Host: ' + NETHOST + ' port: ' + NETPORT);

var TCP_BUFFER_SIZE = Math.pow(2,16);
var spy_emitter = new events.EventEmitter();

if( UNIXPORT != undefined ) {
	fs.unlink(UNIXPORT, function(err) {
		if (err) {
			console.log('Will create UI Unix socket:' + UNIXPORT );
		} else {
			console.log('Successfully deleted UI Unix sockect:' + UNIXPORT );
		};
	});
}

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

spy_emitter.on("spy_display", function(message) {

        if(spyClients[0] !== undefined && spyClients.length !== undefined) {

                console.log('clients[' + spyClients.length + '] ' + ' history[' + spy_Message.length + '] ' + 'message: ' + message );

                for (var i=0; i < spyClients.length; i++) {
//                        console.log('spy_emitter: sending to ' +  i + ' total: '+ spyClients.length);
			spyClients[i].sendUTF(message);

		}
		/* Save spy display messages */
		spy_totMsg++;
		if (spy_totMsg > spy_maxMsgStore)
			spy_Message.shift();
		spy_Message.push(message);
        }
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
	// we need to know client index to remove them on 'close' event
        var index = spyClients.push(connection) - 1;

        console.log((new Date()) + ' Connection from ' + request.origin + ', index: ' + index);
	refreshBrowser(index);

        /* This code is not used
         * No input from spy page is expected
         */
        connection.on('message', function(message) {
                if (message.type === 'utf8') { // accept only text

                        /* get data sent from users web page */
                        var frontendmsg = JSON.parse(message.utf8Data);

                        console.log('Unhandled message type from client: ' + frontendmsg.type);
                }
        });

        // user disconnected
        connection.on('close', function(connection) {
                console.log((new Date()) + " Connection close: Peer "
                            + request.origin + " disconnected for index: " + index);
                // remove user from the list of connected clients
                spyClients.splice(index, 1);
        });
});

/**
 * ===================== network Socket server ========================
 *
 * Create a server instance, and chain the listen function to it
 * The function passed to net.createServer() becomes the event handler for the 'connection' event
 * The sock object the callback function receives is UNIQUE for each connection
 */
net.createServer(function(sock) {


	// We have a connection - a socket object is assigned to the connection automatically
	console.log('UI NET Socket CONNECTED: ' + sock.remoteAddress +':'+ sock.remotePort);

	// To buffer tcp data see:
	// http://stackoverflow.com/questions/7034537/nodejs-what-is-the-proper-way-to-handling-tcp-socket-streams-which-delimiter
	var buf = new Buffer(TCP_BUFFER_SIZE);  //new buffer with size 2^16

	var icount = 0;

        // Add a 'data' event handler to this instance of socket
        sock.on('data', function(data) {
                /* This is a TCP connection so is stream based NOT
                 * message based.
                 * This handler can receive multiple spy packets for
                 * each instance called.
                 */
                var dataStr = data.toString('utf8');

                icount++;

                try {
                        var jsonchk = JSON.parse(data);
                } catch(e) {
                        // Check for TCP stream combining multiple JSON objects
                        var nSearch = dataStr.search("}{");

                        // Does packet contain any concatenated JSON objects?
                        if(nSearch != -1) {
                                // Packet contains at least one concateated object
                                while (nSearch != -1) {
                                        var firstPkt = dataStr.slice(0, nSearch+1);
                                        dataStr = dataStr.slice(nSearch+1);

                                        spy_emitter.emit("spy_display", firstPkt);
                                        // console.log('DATA [' + icount + "] len: " + firstPkt.length + '  ' + firstPkt);
                                        icount++;
                                        nSearch = dataStr.search("}{");
                                }
                        } else {
                                console.log('Parse error on pkt [' + icount + "] len: " + dataStr.length + '  ' + dataStr);
                        }
                }
                spy_emitter.emit("spy_display", dataStr);
                //  console.log('DATA [' + icount + "] len: " + dataStr.length + '  ' + dataStr;

        });

	// Add a 'close' event handler to this instance of socket
	sock.on('close', function(data) {
		console.log('Unix socket connection closed at: ' + (new Date()) + 'socket address: ' + sock.remoteAddress +' '+ sock.remotePort);
	});

}).listen(NETPORT, NETHOST);

console.log((new Date()) + ' UI Socket Server listening on ' + NETHOST +': '+ NETPORT);

/**
 * ===================== HTML server ========================
 */

var connect = require(global_module_dir + 'connect');
connect.createServer(
                     connect.static(__dirname),
                     function(req, res){
        res.setHeader('Content-Type', 'text/html');
        res.end('You need a path, try /spy.html\n');
}
).listen(HTMLPORT);

}); /* End - iniparser wrapper */
