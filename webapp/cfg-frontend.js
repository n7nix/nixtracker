$(function () {
        "use strict";


	/* Address of machine hosting aprs & node.js */
	var WebIpSock="57334";
	/* Get server IP */
	var WebIpAddr = window.location.hostname;

	var status = $('#status');

	// if user is running mozilla then use it's built-in WebSocket
	window.WebSocket = window.WebSocket || window.MozWebSocket;

	// if browser doesn't support WebSocket, just show some notification and exit
	if (!window.WebSocket) {
		messages.html($('<p>', { text: 'Sorry, but your browser doesn\'t '
		+ 'support WebSockets.'} ));
		msginput.hide();
		$('span').hide();
		return;
	}

	var debugWin = $('#debugwin');
	var connstatus = $('#status');
	var input = $('#input');

	// open connection
	var connection = new WebSocket('ws://'+ WebIpAddr+':'+ WebIpSock);


	connection.onopen = function () {
		// first we want users to enter their names
		input.removeAttr('disabled');
		connstatus.text('Connection status:');
		input.val(' Connected ');

		/*
		 * On startup request some config
		 */
		connection.send(JSON.stringify( { type: 'getconfig', data: 'source_callsign'} ));
	};

	connection.onerror = function (error) {
		// just in there were some problems with conenction...
		input.text('<p>', { text: 'Sorry, but there\'s some problem with your '
		+ 'connection or the server is down.</p>' } );
	};

	// most important part - incoming messages
	connection.onmessage = function (message) {

		debugWin.append('<p> got message</p>');

		var msgStr = message.toString('utf8');

		// try to parse JSON message. Because we know that the server always returns
		// JSON this should work without any problem but we should make sure that
		// the massage is not chunked or otherwise damaged.
		try {
			var json = JSON.parse(message.data);
		} catch (e) {
			debugWin.append('CFG: This doesn\'t look like a valid JSON: type: ' + message.type + ",  data: " + message.data);
			console.log('This doesn\'t look like valid JSON: ', message.data);
			return;
		}

		console.log('MSG: type: ' + message.type + ', data: ' + message.data);
		console.log('JSON: ' + json);

		if (json.type === 'msg') {
			var lines = json.data.split(/\r\n|\r|\n/g);
//                        debugWin.append('<p>number of lines: ' + lines.length + '</p>');
			for(var i=0; i < lines.length; i++) {
				addWl2k(lines[i]);
			}

		} else if (json.type === 'cfg') {
			debugWin.append('<p> got cfg</p>');
			document.getElementById("test").replaceChild(
				renderjson(JSON.parse(json.data))
				);

		} else if (json.type === 'cfginit') {
			debugWin.append('<p> got config init</p>');
//			document.getElementById("test").appendChild(
//				renderjson({ hello: [1,2,3,4], there: { a:1, b:2, c:["hello", null] } })
//				);
			document.getElementById("test").appendChild(
				renderjson(JSON.parse(json.data))
				);
		} else {
			debugWin.append('<p>Unhandled message type from server: ' + msgStr + '</p>');
		}
	};

	document.getElementById("outboxBtn").onclick=function(){
		debugWin.append('<p>CheckOutbox</p>');

		connection.send(JSON.stringify( { type: 'cfgready', data: 'needsome'} ));

//		var msg = 'send_button_test';
//		connection.send(JSON.stringify( { type: 'message', data: msg} ));
	};


});
