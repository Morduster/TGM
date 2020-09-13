// for using int_64 data
#define ARDUINOJSON_USE_LONG_LONG 1 
// for decoding UTF8/UNICODE
#define ARDUINOJSON_DECODE_UNICODE 1 

#if defined(ARDUINO_ARCH_ESP8266) // ESP8266
// for strings stored in FLASH - only for ESP8266
#define ARDUINOJSON_ENABLE_PROGMEM 1
#endif
#include <ArduinoJson.h>
#include "CTBot.h"
#include "Utilities.h"

// parameters:
// 1) token
// 2) Telegram API command 
// 3) payload length/size
// 4) content type
#define CTBOT_HEADER_STRING FSTR("POST /bot%s/%s HTTP/1.1\r\nHost: api.telegram.org\r\nContent-Length: %d\r\nContent-Type: %s\r\n\r\n")

// content types
#define CTBOT_CONTENT_TYPE_JSON FSTR("application/json")

// telegram commands
#define CTBOT_COMMAND_GETUPDATES      CFSTR("getUpdates")
#define CTBOT_COMMAND_SENDMESSAGE     CFSTR("sendMessage")
#define CTBOT_COMMAND_ENDQUERY        CFSTR("answerCallbackQuery")
#define CTBOT_COMMAND_GETME           CFSTR("getMe")
#define CTBOT_COMMAND_EDITMESSAGETEXT CFSTR("editMessageText")
#define CTBOT_COMMAND_DELETEMESSAGE   CFSTR("deleteMessage")

CTBot::CTBot() {
	m_token = NULL; // no token
	m_lastUpdate = 0;  // not updated yet
	m_isWaitingResponse = false;
	m_lastUpdateTimeStamp = millis();
}

CTBot::~CTBot() {
	if (m_token != NULL)
		free(m_token);
}

bool CTBot::setTelegramToken(const String& token) {
	return setTelegramToken(token.c_str());
}
bool CTBot::setTelegramToken(const char* token) {
	int tokenSize;
	if (m_token != NULL)
		free(m_token);
	m_token = NULL;

	if (NULL == token) {
		serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->setTelegramToken: token can't be NULL\n"));
		return false;
	}

	tokenSize = strlen_P(token);
	if (0 == tokenSize) {
		serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->setTelegramToken: token too short (%d)\n"), tokenSize);
		return false;
	}

	m_token = (char*)malloc(tokenSize + 1);
	if (NULL == m_token) {
		serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->setTelegramToken: unable to allocate memory\n"));
		return false;
	}

	memccpy_P(m_token, token, 1, tokenSize + 1);

	return true;
}

void CTBot::setStatusPin(int8_t pin) {
	m_wifi.setStatusPin(pin);
	m_connection.setStatusPin(pin);
}

#if ARDUINOJSON_VERSION_MAJOR == 5
bool CTBot::sendCommand(const char* command, const JsonObject& jsonData) {
#elif ARDUINOJSON_VERSION_MAJOR == 6
bool CTBot::sendCommand(const char* command, const DynamicJsonDocument& jsonData) {
#endif
	bool response;
	uint16_t headerSize, payloadSize;
	char* pheader, * ppayload;

	if (m_isWaitingResponse) {
		// awaiting a response...
		if (m_connection.isConnected()) {
			// ...and the connection is active -> wait for response
			serialLog(CTBOT_DEBUG_CONNECTION, CFSTR("--->sendCommand: there is another request awaiting response\n"));
			return false;
		}
		else {
			// ...and connection lost -> go on!
			m_isWaitingResponse = false;
		}
	}

	if (NULL == m_token) {
		serialLog(CTBOT_DEBUG_CONNECTION, CFSTR("--->sendCommand: no Telegram token defined\n"));
		return false;
	}

#if ARDUINOJSON_VERSION_MAJOR == 5
	payloadSize = jsonData.measureLength() + 1;
#elif ARDUINOJSON_VERSION_MAJOR == 6
	payloadSize = measureJson(jsonData) + 1;
#endif

	ppayload = (char*)malloc(payloadSize);
	if (NULL == ppayload) {
		serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->sendCommand: unable to allocate memory\n"));
		return false;
	}

#if ARDUINOJSON_VERSION_MAJOR == 5
	jsonData.printTo(ppayload, payloadSize);
#elif ARDUINOJSON_VERSION_MAJOR == 6
	serializeJson(jsonData, ppayload, payloadSize);
#endif

	// header
	headerSize = snprintf_P(NULL, 0, (char*)CTBOT_HEADER_STRING, m_token, command, payloadSize, CTBOT_CONTENT_TYPE_JSON) + 1;
	pheader = (char*)malloc(headerSize);
	if (NULL == pheader) {
		serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->sendCommand: unable to allocate memory\n"));
		return false;
	}
	snprintf_P(pheader, headerSize, (char*)CTBOT_HEADER_STRING, m_token, command, payloadSize, CTBOT_CONTENT_TYPE_JSON);

	response = m_connection.POST(pheader, (uint8_t*)ppayload, payloadSize);

	serialLog(CTBOT_DEBUG_CONNECTION, "--->sendCommand: Header\n%s\n", pheader);
	serialLog(CTBOT_DEBUG_CONNECTION, "--->sendCommand: Payload\n%s\n", ppayload);

	free(ppayload);
	free(pheader);

	if (response)
		m_isWaitingResponse = true;

	return response;
}

bool CTBot::sendMessageEx(int64_t id, const char* message, const char* keyboard) {
	bool response;

#if ARDUINOJSON_VERSION_MAJOR == 5
#if CTBOT_BUFFER_SIZE > 0
	StaticJsonBuffer<CTBOT_JSON5_BUFFER_SIZE> jsonBuffer;
#else
	DynamicJsonBuffer jsonBuffer;
#endif
	JsonObject& root = jsonBuffer.createObject();
#elif ARDUINOJSON_VERSION_MAJOR == 6
	DynamicJsonDocument root(CTBOT_JSON6_BUFFER_SIZE);
#endif

	// payload
	root[FSTR("chat_id")] = id;
	root[FSTR("text")] = message;

#if ARDUINOJSON_VERSION_MAJOR == 5
#if CTBOT_BUFFER_SIZE > 0
	StaticJsonBuffer<CTBOT_JSON5_BUFFER_SIZE> buf;
#else
	DynamicJsonBuffer buf;
#endif
	JsonObject& myKbd = buf.parse(keyboard);

#elif ARDUINOJSON_VERSION_MAJOR == 6
	DynamicJsonDocument doc(CTBOT_JSON6_BUFFER_SIZE);
	deserializeJson(doc, keyboard, strlen(keyboard));
	JsonObject myKbd = doc.as<JsonObject>();
#endif

	if (keyboard != NULL)
		if (strlen(keyboard) != 0)
			root[FSTR("reply_markup")] = myKbd;

	response = sendCommand(CTBOT_COMMAND_SENDMESSAGE, root);

	return response;
}
bool CTBot::sendMessageEx(int64_t id, const char* message, CTBotInlineKeyboard& keyboard) {
	return sendMessageEx(id, message, keyboard.getJSON());
}
bool CTBot::sendMessageEx(int64_t id, const char* message, CTBotReplyKeyboard& keyboard) {
	return sendMessageEx(id, message, keyboard.getJSON());
}

int32_t CTBot::sendMessage(int64_t id, const char* message, const char* keyboard) {
		CTBotMessageType result = CTBotMessageNoData;
	TBMessage msg;

	if (!sendMessageEx(id, message, keyboard))
//		return false;
		return 0;
	while (m_isWaitingResponse)
		result = parseResponse(msg);

	if (result != CTBotMessageACK)
//		return false;
		return 0;
	return msg.messageID;
//	return true;
}
int32_t CTBot::sendMessage(int64_t id, const char* message, CTBotInlineKeyboard & keyboard) {
	return sendMessage(id, message, keyboard.getJSON());
}
int32_t CTBot::sendMessage(int64_t id, const char* message, CTBotReplyKeyboard & keyboard) {
		return sendMessage(id, message, keyboard.getJSON());
}

bool CTBot::getUpdates() {
	bool response;

	/*
    uint16_t headerSize, payloadSize;
	char *pheader, *ppayload;
*/
	// check if is passed CTBOT_GET_UPDATE_TIMEOUT ms from the last update
	// Telegram server accept updates only every 3 seconds
	uint32_t currentTime = millis();
	if (m_lastUpdateTimeStamp > currentTime) {
		// millis has done an overflow and start over
		if (((UINT32_MAX - m_lastUpdateTimeStamp) + currentTime) < CTBOT_GET_UPDATE_TIMEOUT) {
//			serialLog(CTBOT_DEBUG_CONNECTION, CFSTR("--->getUpdates: too much updates in %ums\n"), CTBOT_GET_UPDATE_TIMEOUT);
			return false;
		}
	}
	else {
		if ((currentTime - m_lastUpdateTimeStamp) < CTBOT_GET_UPDATE_TIMEOUT) {
//			serialLog(CTBOT_DEBUG_CONNECTION, CFSTR("--->getUpdates: too much updates in %ums\n"), CTBOT_GET_UPDATE_TIMEOUT);
			return false;
		}
	}


/*
	if (m_isWaitingResponse) {
		// awaiting a response...
		if (m_connection.isConnected()) {
			// ...and the connection is active -> wait for response
			serialLog(CTBOT_DEBUG_CONNECTION, CFSTR("--->getUpdates: there is another request awaiting response\n"));
			return false;
		}
		else {
			// ...and connection lost -> go on!
			m_isWaitingResponse = false;
		}
	}

	if (NULL == m_token) {
		serialLog(CTBOT_DEBUG_CONNECTION, CFSTR("--->getUpdates: no Telegram token defined\n"));
		return false;
	}
*/

#if ARDUINOJSON_VERSION_MAJOR == 5
#if CTBOT_BUFFER_SIZE > 0
	StaticJsonBuffer<CTBOT_JSON5_BUFFER_SIZE> jsonBuffer;
#else
	DynamicJsonBuffer jsonBuffer;
#endif
	JsonObject& root = jsonBuffer.createObject();
#elif ARDUINOJSON_VERSION_MAJOR == 6
	DynamicJsonDocument root(CTBOT_JSON6_BUFFER_SIZE);
#endif

	// payload
	root[FSTR("limit")]           = 1;
	root[FSTR("allowed_updates")] = FSTR("message,callback_query");
	root[FSTR("offset")]          = m_lastUpdate;

	response = sendCommand(CTBOT_COMMAND_GETUPDATES, root);

	m_lastUpdateTimeStamp = millis();

/*
#if ARDUINOJSON_VERSION_MAJOR == 5
	payloadSize = root.measureLength() + 1;
#elif ARDUINOJSON_VERSION_MAJOR == 6
	payloadSize = measureJson(root) + 1;
#endif

	ppayload = (char*)malloc(payloadSize);
	if (NULL == ppayload) {
		serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->getUpdates: unable to allocate memory\n"));
		return false;
	}

#if ARDUINOJSON_VERSION_MAJOR == 5
	root.printTo(ppayload, payloadSize);
#elif ARDUINOJSON_VERSION_MAJOR == 6
	serializeJson(root, ppayload, payloadSize);
#endif

	// header
	headerSize = snprintf_P(NULL, 0, (char*)CTBOT_HEADER_STRING, m_token, CTBOT_COMMAND_GETUPDATES, payloadSize, CTBOT_CONTENT_TYPE_JSON) + 1;
	pheader = (char*)malloc(headerSize);
	if (NULL == pheader) {
		serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->getUpdates: unable to allocate memory\n"));
		return false;
	}
	snprintf_P(pheader, headerSize, (char*)CTBOT_HEADER_STRING, m_token, CTBOT_COMMAND_GETUPDATES, payloadSize, CTBOT_CONTENT_TYPE_JSON);

	response = m_connection.POST(pheader, (uint8_t*)ppayload, payloadSize);

	if (response) {
		serialLog(CTBOT_DEBUG_CONNECTION, "--->getUpdates: Header\n%s\n", pheader);
		serialLog(CTBOT_DEBUG_CONNECTION, "--->getUpdates: Payload\n%s\n", ppayload);
		m_isWaitingResponse = true;
	}

	free(ppayload);
	free(pheader);
*/
	return response;
}

CTBotMessageType CTBot::parseResponse(TBMessage& message) {
	if (!m_connection.isConnected()) {
		// no active connection -> reset the waiting response variable;
		m_isWaitingResponse = false;
		return CTBotMessageNoData;
	}

	const char* response = m_connection.receive();

	if (NULL == response) {
		message.messageType = CTBotMessageNoData;
		return CTBotMessageNoData;
	}

	serialLog(CTBOT_DEBUG_CONNECTION, CFSTR("--->parseResponse: response received\n"));

	// a response is received
	m_isWaitingResponse = false;

#if ARDUINOJSON_VERSION_MAJOR == 5
#if CTBOT_BUFFER_SIZE > 0
	StaticJsonBuffer<CTBOT_JSON5_BUFFER_SIZE> jsonBuffer;
#else
	DynamicJsonBuffer jsonBuffer;
#endif
	JsonObject& root = jsonBuffer.parse(response);
#elif ARDUINOJSON_VERSION_MAJOR == 6
	DynamicJsonDocument root(CTBOT_JSON6_BUFFER_SIZE);
	DeserializationError error = deserializeJson(root, response);

	if (error) {
		serialLog(CTBOT_DEBUG_JSON, CFSTR("--->parseResponse: ArduinoJson deserialization error code: %s\n"), error.c_str());
		m_connection.flush();
		message.messageType = CTBotMessageNoData;
		return CTBotMessageNoData;
	}
#endif

	// free memory - all the data is now stored in the ArduinoJSON object
	m_connection.flush();

	if (!root[FSTR("ok")]) {
#if (CTBOT_DEBUG_MODE & CTBOT_DEBUG_JSON) > 0
		serialLog(CTBOT_DEBUG_JSON, CFSTR("--->parseResponse: JSON error "));
#if ARDUINOJSON_VERSION_MAJOR == 5
		root.prettyPrintTo(Serial);
#elif ARDUINOJSON_VERSION_MAJOR == 6
		serializeJsonPretty(root, Serial);
#endif
		serialLog(CTBOT_DEBUG_JSON, "\n");
#endif
		message.messageType = CTBotMessageNoData;
		return CTBotMessageNoData;
	}

#if (CTBOT_DEBUG_MODE & CTBOT_DEBUG_JSON) > 0
	serialLog(CTBOT_DEBUG_JSON, CFSTR("--->parseResponse: JSON "));
#if ARDUINOJSON_VERSION_MAJOR == 5
	root.prettyPrintTo(Serial);
#elif ARDUINOJSON_VERSION_MAJOR == 6
	serializeJsonPretty(root, Serial);
#endif
	serialLog(CTBOT_DEBUG_JSON, "\n");
#endif

	uint32_t updateID = root[FSTR("result")][0][FSTR("update_id")].as<int32_t>();
	if (0 == updateID) {
		if (root[FSTR("result")].size() > 0) {
			// no updateID but result not empty -> sendMessage ack
			message.messageID        = root[FSTR("result")][FSTR("message_id")].as<int32_t>();
			message.sender.id        = root[FSTR("result")][FSTR("from")][FSTR("id")].as<int32_t>();
			message.sender.username  = root[FSTR("result")][FSTR("from")][FSTR("username")].as<String>();
			message.sender.firstName = root[FSTR("result")][FSTR("from")][FSTR("first_name")].as<String>();
			message.date             = root[FSTR("result")][FSTR("date")].as<int32_t>();
			message.text             = root[FSTR("result")][FSTR("text")].as<String>();
			message.group.id         = root[FSTR("result")][FSTR("chat")][FSTR("id")].as<int64_t>();
			message.messageType      = CTBotMessageACK;
			return CTBotMessageACK;
		}
		// the field is not present -> it's a sendMessage response
		message.messageType = CTBotMessageNoData;
		return CTBotMessageNoData;
	}
	m_lastUpdate = updateID + 1;

	if (root[FSTR("result")][0][FSTR("callback_query")][FSTR("id")]) {
		// this is a callback query
		message.messageID         = root[FSTR("result")][0][FSTR("callback_query")][FSTR("message")][FSTR("message_id")].as<int32_t>();
		message.text              = root[FSTR("result")][0][FSTR("callback_query")][FSTR("message")][FSTR("text")].as<String>();
		message.date              = root[FSTR("result")][0][FSTR("callback_query")][FSTR("message")][FSTR("date")].as<int32_t>();
		message.sender.id         = root[FSTR("result")][0][FSTR("callback_query")][FSTR("from")][FSTR("id")].as<int32_t>();
		message.sender.username   = root[FSTR("result")][0][FSTR("callback_query")][FSTR("from")][FSTR("username")].as<String>();
		message.sender.firstName  = root[FSTR("result")][0][FSTR("callback_query")][FSTR("from")][FSTR("first_name")].as<String>();
		message.sender.lastName   = root[FSTR("result")][0][FSTR("callback_query")][FSTR("from")][FSTR("last_name")].as<String>();
		message.callbackQueryID   = root[FSTR("result")][0][FSTR("callback_query")][FSTR("id")].as<String>();
		message.callbackQueryData = root[FSTR("result")][0][FSTR("callback_query")][FSTR("data")].as<String>();
		message.chatInstance      = root[FSTR("result")][0][FSTR("callback_query")][FSTR("chat_instance")].as<String>();
		message.messageType = CTBotMessageQuery;

		serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->parseResponse: Free heap memory : %u\n"), ESP.getFreeHeap());

		return CTBotMessageQuery;
	}
	else if (root[FSTR("result")][0][FSTR("message")][FSTR("message_id")]) {
		// this is a message
		message.messageID        = root[FSTR("result")][0][FSTR("message")][FSTR("message_id")].as<int32_t>();
		message.sender.id        = root[FSTR("result")][0][FSTR("message")][FSTR("from")][FSTR("id")].as<int32_t>();
		message.sender.username  = root[FSTR("result")][0][FSTR("message")][FSTR("from")][FSTR("username")].as<String>();
		message.sender.firstName = root[FSTR("result")][0][FSTR("message")][FSTR("from")][FSTR("first_name")].as<String>();
		message.sender.lastName  = root[FSTR("result")][0][FSTR("message")][FSTR("from")][FSTR("last_name")].as<String>();
		message.group.id         = root[FSTR("result")][0][FSTR("message")][FSTR("chat")][FSTR("id")].as<int64_t>();
		message.group.title      = root[FSTR("result")][0][FSTR("message")][FSTR("chat")][FSTR("title")].as<String>();
		message.date             = root[FSTR("result")][0][FSTR("message")][FSTR("date")].as<int32_t>();

#if ARDUINOJSON_VERSION_MAJOR == 5
		if (root[FSTR("result")][0][FSTR("message")][FSTR("text")].as<String>().length() != 0) {
#elif ARDUINOJSON_VERSION_MAJOR == 6
		if (root[FSTR("result")][0][FSTR("message")][FSTR("text")]) {
#endif
			// this is a text message
			message.text        = root[FSTR("result")][0][FSTR("message")][FSTR("text")].as<String>();
			message.messageType = CTBotMessageText;

			serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->parseResponse: Free heap memory : %u\n"), ESP.getFreeHeap());

			return CTBotMessageText;
		}
		else if (root[FSTR("result")][0][FSTR("message")][FSTR("location")]) {
			// this is a location message
			message.location.longitude = root[FSTR("result")][0][FSTR("message")][FSTR("location")][FSTR("longitude")].as<float>();
			message.location.latitude  = root[FSTR("result")][0][FSTR("message")][FSTR("location")][FSTR("latitude")].as<float>();
			message.messageType        = CTBotMessageLocation;

			serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->parseResponse: Free heap memory : %u\n"), ESP.getFreeHeap());

			return CTBotMessageLocation;
		}
		else if (root[FSTR("result")][0][FSTR("message")][FSTR("contact")]) {
			// this is a contact message
			message.contact.id          = root[FSTR("result")][0][FSTR("message")][FSTR("contact")][FSTR("user_id")].as<int32_t>();
			message.contact.firstName   = root[FSTR("result")][0][FSTR("message")][FSTR("contact")][FSTR("first_name")].as<String>();
			message.contact.lastName    = root[FSTR("result")][0][FSTR("message")][FSTR("contact")][FSTR("last_name")].as<String>();
			message.contact.phoneNumber = root[FSTR("result")][0][FSTR("message")][FSTR("contact")][FSTR("phone_number")].as<String>();
			message.contact.vCard       = root[FSTR("result")][0][FSTR("message")][FSTR("contact")][FSTR("vcard")].as<String>();
			message.messageType         = CTBotMessageContact;

			serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->parseResponse: Free heap memory : %u\n"), ESP.getFreeHeap());

			return CTBotMessageContact;
		}
	}

	// no valid/handled message
	message.messageType = CTBotMessageNoData;
	return CTBotMessageNoData;
}
CTBotMessageType CTBot::parseResponse(TBUser& user) {
	if (!m_connection.isConnected()) {
		// no active connection -> reset the waiting response variable;
		m_isWaitingResponse = false;
		return CTBotMessageNoData;
	}

	const char* response = m_connection.receive();

	if (NULL == response) {
		return CTBotMessageNoData;
	}

	serialLog(CTBOT_DEBUG_CONNECTION, CFSTR("--->parseResponse: response received\n"));

	// a response is received
	m_isWaitingResponse = false;

#if ARDUINOJSON_VERSION_MAJOR == 5
#if CTBOT_BUFFER_SIZE > 0
	StaticJsonBuffer<CTBOT_JSON5_BUFFER_SIZE> jsonBuffer;
#else
	DynamicJsonBuffer jsonBuffer;
#endif
	JsonObject& root = jsonBuffer.parse(response);
#elif ARDUINOJSON_VERSION_MAJOR == 6
	DynamicJsonDocument root(CTBOT_JSON6_BUFFER_SIZE);
	DeserializationError error = deserializeJson(root, response);

	if (error) {
		serialLog(CTBOT_DEBUG_JSON, CFSTR("--->parseResponse: ArduinoJson deserialization error code: %s\n"), error.c_str());
		m_connection.flush();
		return CTBotMessageNoData;
	}
#endif

	// free memory - all the data is now stored in the ArduinoJSON object
	m_connection.flush();

	if (!root[FSTR("ok")]) {
#if (CTBOT_DEBUG_MODE & CTBOT_DEBUG_JSON) > 0
		serialLog(CTBOT_DEBUG_JSON, CFSTR("--->parseResponse: JSON error "));
#if ARDUINOJSON_VERSION_MAJOR == 5
		root.prettyPrintTo(Serial);
#elif ARDUINOJSON_VERSION_MAJOR == 6
		serializeJsonPretty(root, Serial);
#endif
		serialLog(CTBOT_DEBUG_JSON, "\n");
#endif
		return CTBotMessageNoData;
	}

#if (CTBOT_DEBUG_MODE & CTBOT_DEBUG_JSON) > 0
	serialLog(CTBOT_DEBUG_JSON, CFSTR("--->parseResponse: JSON "));
#if ARDUINOJSON_VERSION_MAJOR == 5
	root.prettyPrintTo(Serial);
#elif ARDUINOJSON_VERSION_MAJOR == 6
	serializeJsonPretty(root, Serial);
#endif
	serialLog(CTBOT_DEBUG_JSON, "\n");
#endif

	user.firstName    = root[FSTR("result")][FSTR("first_name")].as<String>();
	user.id           = root[FSTR("result")][FSTR("id")].as<int32_t>();
	user.isBot        = root[FSTR("result")][FSTR("is_bot")].as<bool>();
	user.username     = root[FSTR("result")][FSTR("username")].as<String>();

	return CTBotMessageContact;
}

CTBotMessageType CTBot::getNewMessage(TBMessage& message) {
	CTBotMessageType result = CTBotMessageNoData;

	if (!getUpdates())
		return CTBotMessageNoData;
	while (m_isWaitingResponse)
		result = parseResponse(message);
	return result;
}

bool CTBot::endQueryEx(const char* queryID, const char* message, bool alertMode) {
	bool response;

//	uint16_t headerSize, payloadSize;
//	char* pheader, * ppayload;

	if (NULL == queryID)
		return false;
	if (0 == strlen(queryID))
		return false;
/*
	if (m_isWaitingResponse) {
		// awaiting a response...
		if (m_connection.isConnected()) {
			// ...and the connection is active -> wait for response
			serialLog(CTBOT_DEBUG_CONNECTION, CFSTR("--->endQuery: there is another request awaiting response\n"));
			return false;
		}
		else {
			// ...and connection lost -> go on!
			m_isWaitingResponse = false;
		}
	}

	if (NULL == m_token) {
		serialLog(CTBOT_DEBUG_CONNECTION, CFSTR("--->endQuery: no Telegram token defined\n"));
		return false;
	}
*/

#if ARDUINOJSON_VERSION_MAJOR == 5
#if CTBOT_BUFFER_SIZE > 0
	StaticJsonBuffer<CTBOT_JSON5_BUFFER_SIZE> jsonBuffer;
#else
	DynamicJsonBuffer jsonBuffer;
#endif
	JsonObject& root = jsonBuffer.createObject();
#elif ARDUINOJSON_VERSION_MAJOR == 6
	DynamicJsonDocument root(CTBOT_JSON6_BUFFER_SIZE);
#endif

	// payload
	root[FSTR("callback_query_id")] = queryID;

	if (message != NULL) {
		if (strlen(message) != 0) {
			root[FSTR("text")] = message;
			root[FSTR("show_alert")] = alertMode;
		}
	}

	response = sendCommand(CTBOT_COMMAND_ENDQUERY, root);

/*
#if ARDUINOJSON_VERSION_MAJOR == 5
	payloadSize = root.measureLength() + 1;
#elif ARDUINOJSON_VERSION_MAJOR == 6
	payloadSize = measureJson(root) + 1;
#endif

	ppayload = (char*)malloc(payloadSize);
	if (NULL == ppayload) {
		serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->endQuery: unable to allocate memory\n"));
		return false;
	}

#if ARDUINOJSON_VERSION_MAJOR == 5
	root.printTo(ppayload, payloadSize);
#elif ARDUINOJSON_VERSION_MAJOR == 6
	serializeJson(root, ppayload, payloadSize);
#endif

	// header
	headerSize = snprintf_P(NULL, 0, (char*)CTBOT_HEADER_STRING, m_token, CTBOT_COMMAND_ENDQUERY, payloadSize, CTBOT_CONTENT_TYPE_JSON) + 1;
	pheader = (char*)malloc(headerSize);
	if (NULL == pheader) {
		serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->endQuery: unable to allocate memory\n"));
		return false;
	}
	snprintf_P(pheader, headerSize, (char*)CTBOT_HEADER_STRING, m_token, CTBOT_COMMAND_ENDQUERY, payloadSize, CTBOT_CONTENT_TYPE_JSON);

	response = m_connection.POST(pheader, (uint8_t*)ppayload, payloadSize);

	serialLog(CTBOT_DEBUG_CONNECTION, "--->endQuery: Header\n%s\n", pheader);
	serialLog(CTBOT_DEBUG_CONNECTION, "--->endQuery: Payload\n%s\n", ppayload);

	free(ppayload);
	free(pheader);

	if (response)
		m_isWaitingResponse = true;
*/
	return response;
}
bool CTBot::endQuery(const char* queryID, const char* message, bool alertMode) {
	CTBotMessageType result = CTBotMessageNoData;
	TBMessage msg;

	if (!endQueryEx(queryID, message, alertMode))
		return false;
	while (m_isWaitingResponse)
		result = parseResponse(msg);

	if (result != CTBotMessageACK)
		return false;
	return true;
}

bool CTBot::removeReplyKeyboardEx(int64_t id, const char* message, bool selective)
{
	uint16_t kbdSize;
	char* pkbd;
	bool result;

#if ARDUINOJSON_VERSION_MAJOR == 5
	DynamicJsonBuffer jsonBuffer;
	JsonObject& root = jsonBuffer.createObject();
#elif ARDUINOJSON_VERSION_MAJOR == 6
	DynamicJsonDocument root(CTBOT_JSON6_BUFFER_SIZE);
#endif

	root[FSTR("remove_keyboard")] = true;
	root[FSTR("selective")] = selective;

#if ARDUINOJSON_VERSION_MAJOR == 5
	kbdSize = root.measureLength() + 1;
#elif ARDUINOJSON_VERSION_MAJOR == 6
	kbdSize = measureJson(root) + 1;
#endif

	pkbd = (char*)malloc(kbdSize);
	if (NULL == pkbd) {
		serialLog(CTBOT_DEBUG_MEMORY, CFSTR("--->removeReplyKeyboard: unable to allocate memory\n"));
		return false;
	}

#if ARDUINOJSON_VERSION_MAJOR == 5
	root.printTo(pkbd, kbdSize);
#elif ARDUINOJSON_VERSION_MAJOR == 6
	serializeJson(root, pkbd, kbdSize);
#endif

	result = sendMessageEx(id, message, pkbd);
	free(pkbd);
	return result;
}
bool CTBot::removeReplyKeyboard(int64_t id, const char* message, bool selective)
{
	CTBotMessageType result = CTBotMessageNoData;
	TBMessage msg;

	if (!removeReplyKeyboardEx(id, message, selective))
		return false;
	while (m_isWaitingResponse)
		result = parseResponse(msg);

	if (result != CTBotMessageACK)
		return false;
	return true;
}

bool CTBot::getMeEx()
{
	bool response;

#if ARDUINOJSON_VERSION_MAJOR == 5
#if CTBOT_BUFFER_SIZE > 0
	StaticJsonBuffer<CTBOT_JSON5_BUFFER_SIZE> jsonBuffer;
#else
	DynamicJsonBuffer jsonBuffer;
#endif
	JsonObject& root = jsonBuffer.createObject();
#elif ARDUINOJSON_VERSION_MAJOR == 6
	DynamicJsonDocument root(CTBOT_JSON6_BUFFER_SIZE);
#endif

	response = sendCommand(CTBOT_COMMAND_GETME, root);

	return response;
}
bool CTBot::getMe(TBUser& user)
{
	CTBotMessageType result = CTBotMessageNoData;

	if (!getMeEx())
		return false;
	while (m_isWaitingResponse)
		result = parseResponse(user);

	if (CTBotMessageContact == result)
		return true;

	return false;
}

bool CTBot::testConnection(void)
{
	TBUser user;
	return getMe(user);
}

bool CTBot::editMessageTextEx(int64_t id, int32_t messageID, const char* message, const char* keyboard) {
	bool response;

#if ARDUINOJSON_VERSION_MAJOR == 5
#if CTBOT_BUFFER_SIZE > 0
	StaticJsonBuffer<CTBOT_JSON5_BUFFER_SIZE> jsonBuffer;
#else
	DynamicJsonBuffer jsonBuffer;
#endif
	JsonObject& root = jsonBuffer.createObject();
#elif ARDUINOJSON_VERSION_MAJOR == 6
	DynamicJsonDocument root(CTBOT_JSON6_BUFFER_SIZE);
#endif

	// payload
	root[FSTR("chat_id")]     = id;
	root[FSTR("text")]        = message;
	root[FSTR("message_id")] = messageID;

#if ARDUINOJSON_VERSION_MAJOR == 5
#if CTBOT_BUFFER_SIZE > 0
	StaticJsonBuffer<CTBOT_JSON5_BUFFER_SIZE> buf;
#else
	DynamicJsonBuffer buf;
#endif
	JsonObject& myKbd = buf.parse(keyboard);

#elif ARDUINOJSON_VERSION_MAJOR == 6
	DynamicJsonDocument doc(CTBOT_JSON6_BUFFER_SIZE);
	deserializeJson(doc, keyboard, strlen(keyboard));
	JsonObject myKbd = doc.as<JsonObject>();
#endif

	if (keyboard != NULL)
		if (strlen(keyboard) != 0)
			root[FSTR("reply_markup")] = myKbd;

	response = sendCommand(CTBOT_COMMAND_EDITMESSAGETEXT, root);

	return response;
}
bool CTBot::editMessageTextEx(int64_t id, int32_t messageID, const char* message, CTBotInlineKeyboard& keyboard) {
	return editMessageTextEx(id, messageID, message, keyboard.getJSON());
}
bool CTBot::editMessageTextEx(int64_t id, int32_t messageID, const char* message, CTBotReplyKeyboard& keyboard) {
	return editMessageTextEx(id, messageID, message, keyboard.getJSON());
}

bool CTBot::editMessageText(int64_t id, int32_t messageID, const char* message, const char* keyboard) {
	CTBotMessageType result = CTBotMessageNoData;
	TBMessage msg;

	if (!editMessageTextEx(id, messageID, message, keyboard))
		return false;
	while (m_isWaitingResponse)
		result = parseResponse(msg);

	if (result != CTBotMessageACK)
		return false;
	return true;
}
bool CTBot::editMessageText(int64_t id, int32_t messageID, const char* message, CTBotInlineKeyboard& keyboard) {
	return editMessageText(id, messageID, message, keyboard.getJSON());
}
bool CTBot::editMessageText(int64_t id, int32_t messageID, const char* message, CTBotReplyKeyboard& keyboard) {
	return editMessageText(id, messageID, message, keyboard.getJSON());
}

bool CTBot::editMessageText(int64_t id, int32_t messageID, const String& message, const String& keyboard) {
	return editMessageText(id, messageID, message.c_str(), keyboard.c_str());
}
bool CTBot::editMessageText(int64_t id, int32_t messageID, const String& message, CTBotInlineKeyboard& keyboard) {
	return editMessageText(id, messageID, message.c_str(), keyboard.getJSON());
}
bool CTBot::editMessageText(int64_t id, int32_t messageID, const String& message, CTBotReplyKeyboard& keyboard) {
	return editMessageText(id, messageID, message.c_str(), keyboard.getJSON());
}

bool CTBot::deleteMessageEx(int64_t id, int32_t messageID)
{
	bool response;

#if ARDUINOJSON_VERSION_MAJOR == 5
#if CTBOT_BUFFER_SIZE > 0
	StaticJsonBuffer<CTBOT_JSON5_BUFFER_SIZE> jsonBuffer;
#else
	DynamicJsonBuffer jsonBuffer;
#endif
	JsonObject& root = jsonBuffer.createObject();
#elif ARDUINOJSON_VERSION_MAJOR == 6
	DynamicJsonDocument root(CTBOT_JSON6_BUFFER_SIZE);
#endif

	// payload
	root[FSTR("chat_id")] = id;
	root[FSTR("message_id")] = messageID;

	response = sendCommand(CTBOT_COMMAND_DELETEMESSAGE, root);

	return response;
}
bool CTBot::deleteMessage(int64_t id, int32_t messageID)
{
	CTBotMessageType result = CTBotMessageNoData;
	TBMessage msg;

	if (!deleteMessageEx(id, messageID))
		return false;
	while (m_isWaitingResponse)
		result = parseResponse(msg);

	if (result != CTBotMessageACK)
		return false;
	return true;
}



// -----------------------STUBS - for backward compatibility

bool CTBot::setIP(const char* ip, const char* gateway, const char* subnetMask, const char* dns1, const char* dns2) const {
	return m_wifi.setIP(ip, gateway, subnetMask, dns1, dns2);
}
bool CTBot::setIP(const String& ip, const String& gateway, const String& subnetMask, const String& dns1, const String& dns2) const {
	return m_wifi.setIP(ip.c_str(), gateway.c_str(), subnetMask.c_str(), dns1.c_str(), dns2.c_str());
}

bool CTBot::wifiConnect(const char* ssid, const char* password) {
	return m_wifi.connect(ssid, password);
}
bool CTBot::wifiConnect(const String& ssid, const String& password) {
	return m_wifi.connect(ssid.c_str(), password.c_str());
}

void CTBot::setMaxConnectionRetries(uint8_t retries) {
	m_wifi.setMaxConnectionRetries(retries);
}

bool CTBot::useDNS(bool value) {
	return m_connection.useDNS(value);
}

void CTBot::setFingerprint(const uint8_t* newFingerprint) {
	m_connection.setFingerprint(newFingerprint);
}

int32_t CTBot::sendMessage(int64_t id, const String& message, const String& keyboard) {
	return sendMessage(id, message.c_str(), keyboard.c_str());
}
int32_t CTBot::sendMessage(int64_t id, const String& message, CTBotInlineKeyboard& keyboard) {
	return sendMessage(id, message.c_str(), keyboard.getJSON());
}
int32_t CTBot::sendMessage(int64_t id, const String& message, CTBotReplyKeyboard& keyboard) {
	return sendMessage(id, message.c_str(), keyboard.getJSON());
}

CTBotMessageType CTBot::getNewMessage(TBMessage& message, bool blocking) {
	if (blocking)
		delay(CTBOT_GET_UPDATE_TIMEOUT);
	return getNewMessage(message);
}

bool CTBot::removeReplyKeyboard(int64_t id, const String& message, bool selective) {
	return removeReplyKeyboard(id, message.c_str(), selective);
}

void CTBot::enableUTF8Encoding(bool value) {
	value = value;
}

bool CTBot::endQuery(const String& queryID, const String& message, bool alertMode) {
	return endQuery(queryID.c_str(), message.c_str(), alertMode);
}

