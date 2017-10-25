// http://ejohn.org/blog/ecmascript-5-strict-mode-json-and-more/
"use strict";

// Optional. You will see this name in eg. 'ps' or 'top' command
process.title = 'plu-server';

// Install node modules globally npm -g install <module_name>
var global_module_dir='/usr/local/lib/node_modules/';

var uidNumber = require(global_module_dir + "uid-number");
var fs = require('fs');
var exec = require('child_process').exec;
var iniparser = require(global_module_dir + 'iniparser');

/**
 * Global variables
 */
//var plu_command = "/home/gunn/bin/coretemp.sh";
var plu_command = "/usr/local/bin/wl2ktelnet";
var plu_args = '';
var OUTBOX_DIR='/usr/local/var/wl2k/outbox/';

var plu_uid;
var plu_gid;
var userName;
var groupName;
// list of currently connected clients (users)
var pluClients = [];

//var plu_command = "ls";
//var plu_args = '-salt';

/* *** Web socket server port
 * NOTE: this is also set in plu-frontend.js
 */
var webSocketPort = 1340;

// *** HTML server
var HTMLPORT = 8083;

// websocket and http servers
var webSocketServer = require(global_module_dir + 'websocket').server;
var http = require('http');
var events = require("events");

/**
 * Helper function for escaping html special characters
 */
function htmlEntities(str) {
        return String(str).replace(/&/g, '&amp;').replace(/</g, '&lt;')
                        .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
};

function exec_command (chillens, connection, command) {

        var sys = require('sys');
//        var command = 'ls -lt '+ OUTBOX_DIR;
        //        var command = 'tail -f /var/log/syslog';

        console.log('Yeah, button depressed for cmd: ' + command + '\n');

        var new_child = exec(command, function (error, stdout, stderr) {
                sys.print('stdout: ' + stdout);
                var dataStr = stdout.toString('utf8');

                var jmsg = {
                        type: 'msg',
                              data: dataStr
                };
                var jmsgstr = JSON.stringify( jmsg );

                console.log('json msg: ' + jmsgstr + '\n');

                connection.sendUTF(jmsgstr);

                if(stderr !== null) {
                        sys.print('stderr: ' + stderr);
                }
                if (error !== null) {
                        console.log('exec error: ' + error);
                }
        });

        chillens.push(new_child);

        new_child.on('exit', function() {
                var index = chillens.indexOf(new_child);
                chillens.splice(index, 1);
                console.log('process 1 exit, remaining processes: ' + chillens.length + '\n');
        });
};

function spawn_command (chillens, connection, progname) {

        var child_process = require('child_process');

//        var plu_uid = process.getuid();
//        var plu_gid = process.getgid();

        console.log(' button depressed for: ' + progname + ', strlen args: ' + plu_args.length + '\n');

        if(plu_args.length == 0) {
                console.log('child_process: ' + progname + ' with no args\n');
                var new_child = child_process.spawn(progname, [], {
                        cwd: '.',
                             uid: plu_uid,
                             gid: plu_gid
                });
        } else {
                console.log('child_process: ' + progname + ' with args: ' + plu_args + '\n');
                var new_child = child_process.spawn(progname, [plu_args], {
                        cwd: '.',
                             uid: plu_uid,
                             gid: plu_gid
                });
        }
        chillens.push(new_child);


        new_child.on('exit', function() {
                var index = chillens.indexOf(new_child);
                chillens.splice(index, 1);
                console.log('process exit 2, remaining processes: ' + chillens.length + '\n');
        });

        new_child.stdout.on('data', function(data) {
                process.stdout.write('' + data);
                var dataStr = htmlEntities(data.toString('utf8'));

                var jmsg = {
                        type: 'msg',
                              data: dataStr
                };
                var jmsgstr = JSON.stringify( jmsg );

                console.log('json msg stdout: ' + jmsgstr + '\n');

                connection.sendUTF(jmsgstr);

        });

        new_child.stderr.on('data', function(data) {
                process.stderr.write('' + data);
                var dataStr = htmlEntities(data.toString('utf8'));

                var jmsg = {
                        type: 'msg',
                              data: dataStr
                };
                var jmsgstr = JSON.stringify( jmsg );

                console.log('json msg stderr: ' + jmsgstr + '\n');

                connection.sendUTF(jmsgstr);


        });
};

/*
 * ============ End functions - Begin Main ============
 */

if(userName == undefined) {
	userName = "gunn";
}

if(groupName == undefined) {
	groupName = userName;
}

uidNumber(userName, groupName, function(err, plu_uid, plu_gid) {
	if (err) {
		console.log('Error: failed to get uid/gid for ' + userName +  '/' + groupname);
		return;
	}
	console.log('Return from uidNumber, user id: ' + plu_uid + '  group id: ' + plu_gid);
})

/*
 * parse command line for an ini file name
 */
/* define default ini file name */
var ini_file='./plu.ini';
if( process.argv[2] !== undefined) {
	ini_file=process.argv[2];
}

fs.exists(ini_file, function(exists) {
	if (!exists) {
		console.log('Can not open ' + ini_file + ' will use DEFAULTS.');
	}
});


/**
 * Get config from ini file
 **/
iniparser.parse(ini_file, function(err, data) {
	if (err) {
//		console.log('Error: %s, failed to read %s', err.code, err.path);
		console.log('Using defaults: web port: ' + webSocketPort + ' html port: ' + HTMLPORT + ' user name: ' + userName + ' group name: ' + groupName);
	} else {
		/*
		 * Dump all the ini file parameters
		 */
		//        var util = require('util');
		//        var ds = util.inspect(data);
		//        console.log('iniparse test, ui_net: ' + ds);
		if( data.ui_net.websock_port === 'undefined' ) {
			console.log('ini parse error: No web socket port defined, using default: ' + webSocketPort);
		} else {
			webSocketPort = data.ui_net.websock_port;
		}

		if( data.ui_net.html_port === 'undefined' ) {
			console.log('ini parse error: No html port defined, using default: ' + HTMLPORT);
		} else {
			HTMLPORT = data.ui_net.html_port;
		}

		if( data.login.name === 'undefined' ) {
			console.log('ini parse error: No user name defined, using default: ' + userName);
		} else {
			userName = data.login.name;
		}

		if( data.group.name === 'undefined' ) {
			console.log('ini parse error: No group name defined, using default: ' + groupName);
		} else {
			groupName = data.group.name;
		}
	}
	console.log("Connections: websocket port: " + webSocketPort);


/**
   * HTTP server
   */
var server = http.createServer(function(request, response) {
        // Not important for us. We are using a WebSocket server, not HTTP server
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
        httpServer: server
});

// This callback function is called every time someone
// tries to connect to the WebSocket server
wsServer.on('request', function(request) {
        var chillens = [];

        // accept connection - you should check 'request.origin' to make sure that
        // client is connecting from your website
        // (http://en.wikipedia.org/wiki/Same_origin_policy)
        var connection = request.accept(null, request.origin);
        // we need to know client index to remove them on 'close' event
        var index = pluClients.push(connection) - 1;
        var curIndex =0;
        var userName = false;
        var destName = false;

	console.log((new Date()) + ' Connection from origin ' + request.origin + ' accepted for index: ' + index);

        //Test if anyone has logged in yet
        if(pluClients[0] !== undefined && pluClients[0].length !== undefined) {
                console.log("ws: Client length: " +  pluClients[0].length );
        } else {
                console.log("ws: NO Client");
        }
	/* Check count of filenames in outbox */
	var fileNames = fs.readdirSync(OUTBOX_DIR);
	/* Change button color if there are any files in the outbox.*/
	var btnColor = (fileNames.length > 0) ? 'green' : '';
	connection.sendUTF(JSON.stringify({ type:'color', data: btnColor }));

	console.log('Found ' + fileNames.length + ' files in outbox');

        // user sent some message
        connection.on('message', function(message) {

                console.log("connection: received message");

                if (message.type === 'utf8') { // accept only text

                        /* get data sent from users web page */
                        console.log('Parse message: ' + message.utf8Data);

                        var frontendmsg = JSON.parse(message.utf8Data);
                        console.log('Parse message 2: ' + frontendmsg.type);

                        if(frontendmsg.type === "config") {
                                var json = JSON.stringify({ type:'config', data: frontendmsg.data});
                                console.log('Sending config to radio: length =' + json.length + ' data: ' + frontendmsg.data);

				msg_emitter.emit("aprs_cfg", json);

                        }  else if(frontendmsg.type === "message") {

                                // log and broadcast the message
                                console.log((new Date()) + ' Received Message from '
                                            + userName + ': ' + frontendmsg.data);

                                if(frontendmsg.data === "outbox_check") {

                                        exec_command(chillens, connection, 'ls -lt '+ OUTBOX_DIR);

				} else if(frontendmsg.data === "send_button_mheard") {

					exec_command(chillens, connection, 'mheard');

                                } else if(frontendmsg.data === "send_button_telnet") {

                                        spawn_command(chillens, connection, "wl2ktelnet");

                                } else if(frontendmsg.data === "send_button_ax25") {

                                        spawn_command(chillens, connection, "/usr/local/bin/wl2kax25");

                                } else if(frontendmsg.data === "send_button_serial") {

                                        spawn_command(chillens, connection, "wl2kserial");

                                } else if (frontendmsg.data === "send_button_kill_process") {

                                        if(chillens.length != 0) {
                                                killWorkers();
                                        } else {
						console.log("kill button pushed but no child processes running\n");
                                        }
				} else {
					console.log("Button: " + frontendmsg.data + " undefined\n");
				}

                        } else if (frontendmsg.type === "wl2kargs") {

                                plu_args = frontendmsg.data;
                                console.log("connection: received wl2kargs");

                                /* get data sent from users web page */
                                console.log('Parse wl2kargs: ' + plu_args);

                        } else {
                                console.log('Unhandled message type from client: ' + frontendmsg.type);
                        }
                }
        });

        var killWorkers = function() {
                chillens.forEach(function(child) {
                        if (typeof child !== 'undefined') {
                                process.kill(child.pid, 'SIGINT');
                                var index = chillens.indexOf(child);
                                chillens.splice(index, 1);

                                console.log('killed process: ' + child.pid + ', remaining processes: ' + chillens.length + '\n');
                        }
                });
        };

        // user disconnected
        connection.on('close', function(connection) {
                if (userName !== false) {
                        console.log((new Date()) + " Peer "
                                    + connection.remoteAddress + " disconnected.");
                        // remove user from the list of connected clients
                        pluClients.splice(index, 1);
                }
        });

        fs.watch(OUTBOX_DIR, function(event, filename) {
		var fileNames = fs.readdirSync(OUTBOX_DIR);

                console.log('fs watch: event: ' + event + ' filename: ' + filename + ' Number of files: ' + fileNames.length);

                /* Change button color if there are any files in the
                 * outbox.*/
                var btnColor = (fileNames.length > 0) ? 'green' : '';
                connection.sendUTF(JSON.stringify({ type:'color', data: btnColor }));
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
        res.end('You need a path, try /plu.html\n');
}
).listen(HTMLPORT);
console.log('HTML Server listening on port: ' + HTMLPORT);

}); /* End - iniparser wrapper */
