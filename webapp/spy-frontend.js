/**
 * Helper function for escaping spy content
 */
function htmlEntities(str) {
        return String(str).replace(/&/g, '&amp;').replace(/</g, '&lt;')
			.replace(/>/g, '&gt;').replace(/"/g, '&quot;')
			.replace(/(\r\n|\n|\r)/g, '<br/>')
			.replace(/\s/g, '&nbsp;');
// .replace(/\s+/g, '.');
}

$(function () {
	"use strict";

        /* Get server IP */
        var WebIpAddr = window.location.hostname;

	// for better performance - to avoid searching in DOM

	var input = $('#input');
        var status = $('#status');
        var spywin = $('#spywin');
        var spyDebugWin = $('#debugwin');
        var pktCount = $('#packet_counter');

	// my color assigned by the server
	var myColor = false;
        // my name sent to the server
        var myName = false;
        // destination name sent to the server
        var destName = false;
	// Interval clock Id
        var clockID = 0;
        // maximum number of APRS message to display in content DIV
        var maxMsgItems = 1000;

        /* Address of machine hosting aprs-ax25 & node.js */
        var WebIpSock="57334";
        spyDebugWin.append('<p>' + 'Using hostname: ' + WebIpAddr + ' with sock: ' + WebIpSock + '</p>');

	// if user is running mozilla then use it's built-in WebSocket
	window.WebSocket = window.WebSocket || window.MozWebSocket;

	// Does browser support WebSockets?
	if (!window.WebSocket) {
		content.html($('<p>', { text: 'Sorry, but your browser doesn\'t '
		+ 'support WebSockets.'} ));
		input.hide();
		$('span').hide();
		return;
	}

	// open connection
	var connection = new WebSocket('ws://'+ WebIpAddr+':'+WebIpSock);

	connection.onopen = function () {
		// first we want users to enter their names
		input.removeAttr('disabled');
		status.text('Connection status:');
	};

	connection.onerror = function (error) {
		// just in case there were some problems with conenction...
		content.html($('<p>', { text: 'Sorry, but there\'s some problem with your '
		+ 'connection or the server is down.</p>' } ));
	};

	// most important part - incoming messages
        connection.onmessage = function (message) {
                // try to parse JSON message. Because we know that the server always returns
                // JSON this should work without any problem but we should make sure that
                // the massage is not chunked or otherwise damaged.
                try {
                        var json = JSON.parse(message.data);
                } catch (e) {
                        spyDebugWin.append('This doesn\'t look like a valid JSON: ' + message.data);
                        console.log('This doesn\'t look like a valid JSON: ', message.data);
                        return;
                }

                // NOTE: if you're not sure about the JSON structure
                // check the server source code
                if (json.type === 'message') { // it's a single message
                        input.removeAttr('disabled'); // let the user write another message

                        content.remove("p:gt(2)");
                        addMessage(json.data.from, json.data.text,
                                   json.data.color, new Date(json.data.time));


                } else if (json.spy) {
                        /* this statement will throw an error:
                         * * "Uncaught SyntaxError: Unexpected token {"
                         * due to some aprs content */

                        var n = $("#spywin").children().length;

                        /* Clean up any html reserved characters that
                         * may be embedded in spy packet */
//                        var json2=json.spy.replace(/\</g, '&lt;').replace(/\>/g, '&gt;');

                        var json2=htmlEntities(json.spy);

                        //                                spyDebugWin.text('less than: ' + json2);

                        spywin.append('<div class="spy">'
                                      + json2 + ' </div>');
                        spywin.scrollTop(spywin[0].scrollHeight);

                        if (n > maxMsgItems) {
                                $("#spywin").children("div:first").remove()
			}
                        /* display packet count */
                        pktCount.text(json.count);
                } else {
                        spyDebugWin.append('<p>Unhandled: ' + 'name: ' + json.type + '</p>' );
                        console.log('Hmm..., I\'ve never seen JSON like this: ', JSON.stringify(json));
                }

};

	/**
	 * Send mesage when user presses Enter key
	 */
	input.keydown(function(e) {
		if (e.keyCode === 13) {
			var msg = $(this).val();
			if (!msg) {
				return;
			}
			// send the message as an ordinary text
//			connection.send(msg);
			$(this).val('');
			// disable the input field to make the user wait until server
			// sends back response
			input.attr('disabled', 'disabled');

			/* we know that the first message sent from a
			 *  user is their call sign
			 */
			if (myName === false) {
                                myName = msg;
                                connection.send(JSON.stringify( { type: 'callsign', data: msg} ));
                        } else if (destName === false) {
                                destName = msg;
                                connection.send(JSON.stringify( { type: 'sendto', data: msg} ));

                        } else {
                                destName = false;
                                connection.send(JSON.stringify( { type: 'message', data: msg} ));
//                                connection.send(msg);
                        }
		}
	});

	/**
	 * This method is optional. If the server wasn't able to respond to the
	 * in 3 seconds then show some error message to notify the user that
	 * something is wrong.
	 */
	setInterval(function() {
		if (connection.readyState !== 1) {
			status.text('Error');
			input.attr('disabled', 'disabled').val('Unable to communicate '
				+ 'with the WebSocket server.');
		}
	}, 3000);

	/**
	 * Add message to the chat window
	 */
        function addMessage(from, message, color, dt) {
                var pars = $("#content div");
                var n = $("#content").children().length;
                spyDebugWin.append('<p>' + 'count elements ' + pars.length + '  childs: ' + n + '</p>');

                content.append('<div class="item"><span style="color:' + color + '">' + from + '</span> @ ' +
			       + (dt.getHours() < 10 ? '0' + dt.getHours() : dt.getHours()) + ':'
			       + (dt.getMinutes() < 10 ? '0' + dt.getMinutes() : dt.getMinutes())
                               + ': ' + message + '</div>');
                if (n > maxMsgItems) {
                        $("#content").children("div:first").remove()
                }
        }

});
