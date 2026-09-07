#include "BleSerialClient.h"
using namespace std;

ByteRingBuffer<RX_BUFFER_SIZE> BleSerialClient::receiveBuffer;



static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {


    log_i("Notify callback for characteristic ");
    log_i("%s",pBLERemoteCharacteristic->getUUID().toString().c_str());

  // std::string myString = pBLERemoteCharacteristic->readValue();
  // length = myString.length();  
  // pData = reinterpret_cast<uint8_t*>(&myString[0]);


    log_i(" of data length %d", length);

		for (int i = 0; i < length; i++){
       log_i("adding data to buffer %#04x", pData[i]);
		  	BleSerialClient::receiveBuffer.add(pData[i]); 
        }  
#ifdef DEBUG
    Serial.println("Reading:");
    char strBuf[50];
    for (int i = 0; i < length; i++){ 
        sprintf(strBuf, " - 0x%02x",pData[i]);
        Serial.print(strBuf);
    }
    Serial.println();
#endif

}  // notifiy callback

static void scanCompleteCB(BLEScanResults scanResults) {
  // Camper Control: the original restarted the scan from inside this callback,
  // which kept the radio busy even while connected. bleLoop() now decides when
  // to look again, so all we do here is release the result list.
  log_i("scan complete, %d devices", scanResults.getCount());
  BLEDevice::getScan()->clearResults();
} // scanCompleteCB

void BleSerialClient::onConnect(BLEClient *pClient)
{
  log_i("In onConnect()");
	bleConnected = true;
	if (enableLed)
		digitalWrite(ledPin, HIGH);
}  // onConnect

void BleSerialClient::onDisconnect(BLEClient *pClient)
{
  log_i("In onDisconnect()");
	bleConnected = false;
	if (enableLed)
		digitalWrite(ledPin, LOW);
    // Camper Control: the original delay(5000) here stalled a BLE stack
    // callback, which took the whole UI down with it.
    this->receiveBuffer.clear();
    this->TxCharacteristic = nullptr;
    this->RxCharacteristic = nullptr;

} // onDisconnect

bool BleSerialClient::connectToServer() {
    log_i("Forming a connection to ");
    #ifdef DEBUG
    Serial.println(myDevice->getAddress().toString().c_str());
    #endif
    
   // pClient  = BLEDevice::createClient();  // Client Created in begin
    log_i(" - Created client");
   
    pClient->setMTU(517); //set client to request maximum MTU from server (default is 23 otherwise)
  
   // Connect to the remote BLE Server.
    // Camper Control: the original discarded this bool and logged success
    // regardless. On a failed connect it then called getService() on a dead
    // handle, which blocks on a Bluedroid semaphore waiting for a discovery
    // event that never arrives - and because integrations run on the main
    // loop, that froze the UI and left the panel dark. Give up and retry.
    if (!pClient->connect(myDevice)) {
      log_w(" - connect failed, will retry");
      return false;
    }
    log_i(" - Connected to server");
   
  
    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      log_i("Failed to find our service UUID: ");
      log_i("%s",serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    log_i(" - Found our service");


    // Obtain a reference to the Read characteristic in the service of the remote BLE server.
    RxCharacteristic = pRemoteService->getCharacteristic(charRxUUID);
    if (RxCharacteristic == nullptr) {
      log_i("Failed to find our Read characteristic UUID: ");
      log_i("%s",charRxUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    log_i(" - Found our Rx characteristic");
    bleConnected = true;
    // Read the value of the characteristic.
    if(RxCharacteristic->canRead()) {
        log_i("canRead true");
      std::string value = RxCharacteristic->readValue().c_str(); // Explicit conversion
      log_i("The characteristic value was: ");
      log_i("%s",value.c_str());
    }

    if(RxCharacteristic->canNotify())
      RxCharacteristic->registerForNotify(notifyCallback);
    
// Obtain a reference to the Write characteristic in the service of the remote BLE server.

    TxCharacteristic = pRemoteService->getCharacteristic(charTxUUID);
    if (TxCharacteristic == nullptr) {
      #ifdef DEBUG
      Serial.print("Failed to find our Write characteristic UUID: ");
      Serial.println(charTxUUID.toString().c_str());
      #endif
      pClient->disconnect();
      return false;
    }
    #ifdef DEBUG
    Serial.println(" - Found our Tx characteristic");
  
    if(TxCharacteristic->canWrite()) {
         Serial.println("canWrite true");
    }
    #endif  
    return true;
} // connectToServer

 

  void BleSerialClient::onResult(BLEAdvertisedDevice advertisedDevice) {
    #ifdef DEBUG
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());
    #endif
    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
      if (!targetAddress.empty()) {
        // toString() is std::string on core 2.x and String on 3.x; c_str() suits both.
        std::string found = advertisedDevice.getAddress().toString().c_str();
        for (auto &ch : found) ch = tolower(ch);
        if (found != targetAddress) {
          log_i("ignoring BMS %s, waiting for %s", found.c_str(), targetAddress.c_str());
          return;
        }
      }
    
      pBLEScan->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    }
  } // onResult

void BleSerialClient::setTargetAddress(const char *mac)
{
  targetAddress.clear();
  if (mac == nullptr) return;
  for (const char *p = mac; *p; ++p) targetAddress.push_back(tolower(*p));
}

void BleSerialClient::begin(const char *name, bool enable_led, int led_pin)
{
  log_i("In begin()");
	enableLed = enable_led;
	ledPin = led_pin;
  serviceUUID = BLEUUID().fromString(BLE_SERIAL_SERVICE_UUID);
  charRxUUID = BLEUUID().fromString(BLE_RX_UUID);
  charTxUUID = BLEUUID().fromString(BLE_TX_UUID);
	if (enableLed)
	{
    pinMode(ledPin, OUTPUT);
	}
  #ifdef DEBUG
  Serial.println("Starting BLE Client application...");
  #endif
	pBLEDevice = new BLEDevice();
  pBLEDevice->init("");
  pClient = pBLEDevice->createClient();
  pClient->setClientCallbacks(this);

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.

  pBLEScan = pBLEDevice->getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(this);
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  lastScanStartMs = millis();
  pBLEScan->start(5, scanCompleteCB, false);
  log_i("begin exit");

}


void BleSerialClient::bleLoop () {

 if (millis() - flush_100ms >= flush_time) flush() ; // flush every 100ms if no new data

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      #ifdef DEBUG
      Serial.println("We are now connected to the BLE Server.");
      #endif
      this->receiveBuffer.clear();
    } 
    #ifdef DEBUG
    else {

      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    #endif
    doConnect = false;
  }

  if (bleConnected==true) {
    // log_i("we are connencted");  // per-loop spam: floods the UART
    // onRead (RxCharacteristic);
  } else if (doScan) {
    // Camper Control: asynchronous rescan, rate limited. The original called the
    // blocking form of start(), which froze the caller for five seconds every
    // pass while the BMS was out of range.
    if (millis() - lastScanStartMs >= 6000) {
      log_i("ble disconnected, rescanning");
      lastScanStartMs = millis();
      BLEDevice::getScan()->start(5, scanCompleteCB, false);
    }
  }
  
  
}


bool BleSerialClient::connected()
{
	return bleConnected;
}


int BleSerialClient::read()
{
  log_i("In read()");
	uint8_t result = this->receiveBuffer.pop();
	if (result == (uint8_t)'\n')
	{
		this->numAvailableLines--;
	}
	return result;
}

size_t BleSerialClient::readBytes(uint8_t *buffer, size_t bufferSize)
{
  log_i("readBytes(uint8_t *buffer, size_t bufferSize)");
	int i = 0;
	while (i < bufferSize && available())
	{
		buffer[i] = this->receiveBuffer.pop();
		i++;
	}
	return i;
}

int BleSerialClient::peek()
{
  log_i("In peek()");
	if (this->receiveBuffer.getLength() == 0)
		return -1;
	return this->receiveBuffer.get(0);
}

int BleSerialClient::available()
{
  //  log_i("available %d",receiveBuffer.getLength());
  	return receiveBuffer.getLength();
}

size_t BleSerialClient::print(const char *str)
{
 //  Serial.println("In print()");
	if (pClient->isConnected() == 0)
	{
		return 0;
	}

	size_t written = 0;
	for (size_t i = 0; str[i] != '\0'; i++)
	{
		written += this->write(str[i]);
	}
	// Flush if buffer full or if 100ms since last flush
	  if ((this->transmitBufferLength == maxTransferSize)||((millis() - flush_100ms >= flush_time))) { 
      
		  flush();
      }
	return written;
}

size_t BleSerialClient::write(const uint8_t *buffer, size_t bufferSize)
{
  log_i("In write(const uint8_t *buffer, size_t bufferSize)");
	if (pClient->isConnected() == 0)
	{
    log_i("in write pClient is disconnected");
		return 0;
	}

	if (maxTransferSize < MIN_MTU)
	{
		int oldTransferSize = maxTransferSize;
		MTU = pClient->getMTU() - 5;
		maxTransferSize = MTU > BLE_BUFFER_SIZE ? BLE_BUFFER_SIZE : MTU;

		if (maxTransferSize != oldTransferSize)
		{
			log_i("Max BLE transfer size set to %u", maxTransferSize);
		}
	}

	if (maxTransferSize < MIN_MTU)
	{
		return 0;
	}

	size_t written = 0;
	for (int i = 0; i < bufferSize; i++)
	{
    log_i("about to write");
		written += this->write(buffer[i]);
	}
	// Flush if buffer full or if 100ms since last flush
	  if ((this->transmitBufferLength == maxTransferSize)||((millis() - flush_100ms >= flush_time))) { 
      
		  flush();
      }
	return written;
}

size_t BleSerialClient::write(uint8_t byte)
{
 log_i("In write(uint8_t byte)");
	if (pClient->isConnected() == 0)
	{
    log_e("Not Connected");
		return 0;
	}
    log_i("%#04x",byte);
	this->transmitBuffer[this->transmitBufferLength] = byte;
	this->transmitBufferLength++;
  // Flush if buffer full or if 100ms since last flush
	  if ((this->transmitBufferLength == maxTransferSize)||((millis() - flush_100ms >= flush_time))) { 
      
		  // flush();
      }
  	return 1;
}

void BleSerialClient::flush()
{
 
 if (true) { 
	    if (this->transmitBufferLength > 0 && TxCharacteristic != nullptr && bleConnected)
	        {   log_i("flushing %d bytes",this->transmitBufferLength);
		          TxCharacteristic->writeValue(this->transmitBuffer, this->transmitBufferLength);
              this->transmitBufferLength = 0;
          } // buffer > 0
      else if (this->transmitBufferLength > 0)
          {   // Not connected any more - drop the partial frame rather than
              // dereferencing a stale characteristic.
              this->transmitBufferLength = 0;
          }

flush_100ms = millis();     

  } // max buffer || 100ms
} // flush



void BleSerialClient::end()
{
	pBLEDevice->deinit();

}

BleSerialClient::BleSerialClient()
{
  
}

/*
void BleSerialClient::onRead(BLERemoteCharacteristic *pCharacteristic)
{
  log_e("onRead UUID %s",charRxUUID.toString().c_str());
  std::string value = "";
	if  (pCharacteristic->getUUID().toString() == charRxUUID.toString())
	{
    log_i("UUID match - reading value");
    if (pCharacteristic->canRead()) {
        log_e("can read");
         
          value = value + pCharacteristic->readValue();
        log_e("%d",value.length());
        log_e("%s",value.c_str());
		        for (int i = 0; i < value.length(); i++)
			      receiveBuffer.add(value[i]);
    } // can read
  }  // correct UUID
} // onRead
*/
