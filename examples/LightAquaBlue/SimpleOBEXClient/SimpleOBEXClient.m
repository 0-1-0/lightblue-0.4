#import "SimpleOBEXClient.h"
#import <LightAquaBlue/LightAquaBlue.h>

#import <IOBluetooth/IOBluetoothUtilities.h>
#import <IOBluetoothUI/objc/IOBluetoothServiceBrowserController.h>

@implementation SimpleOBEXClient

- (void)awakeFromNib
{
    [[connectButton window] center];
    [[connectButton window] performSelector:@selector(makeFirstResponder:)
                                 withObject:connectButton
                                 afterDelay:0.0];
}

- (void)log:(NSString *)text
{
    [logTextView insertText:text];
    [logTextView insertText:@"\n"];
}

- (NSNumber *)getSizeOfFile:(NSString *)filePath
{
    NSFileManager *fileManager = [NSFileManager defaultManager];
    NSDictionary *fileAttributes = [fileManager fileAttributesAtPath:filePath traverseLink:YES];
    if (fileAttributes != nil)
        return [fileAttributes objectForKey:NSFileSize];
    return nil;
}

- (IBAction)findService:(id)sender
{
    IOBluetoothServiceBrowserController *browser = 
        [IOBluetoothServiceBrowserController serviceBrowserController:0];
    
    if ([browser runModal] == kIOBluetoothUISuccess) {
        IOBluetoothSDPServiceRecord *service = [[browser getResults] objectAtIndex:0];
        [addressField setStringValue:[[service getDevice] getAddressString]];
        BluetoothRFCOMMChannelID channelID;
        if ([service getRFCOMMChannelID:&channelID] == kIOReturnSuccess)
            [channelField setIntValue:channelID];
    }
}

- (IBAction)connectOrDisconnect:(id)sender
{
    if (!mClient) {
        if (![BBLocalDevice isPoweredOn]) {
            [self log:@"Bluetooth device is not available!"];
            return;
        }
        
        NSString *deviceAddressString = [addressField stringValue];
        BluetoothDeviceAddress deviceAddress;
        if (IOBluetoothNSStringToDeviceAddress(deviceAddressString, &deviceAddress) != kIOReturnSuccess) {
            [self log:[NSString stringWithFormat:@"%@ is not a valid Bluetooth device address!", 
                deviceAddressString]];
            return;
        }
        
        // Create a BBBluetoothOBEXClient that will connect to the OBEX
        // server on the specified address and channel.
        mClient = [[BBBluetoothOBEXClient alloc] initWithRemoteDeviceAddress:&deviceAddress
                                                                   channelID:[channelField intValue]
                                                                    delegate:self];        
    }
        
    if (![mClient isConnected]) {
        [self log:[NSString stringWithFormat:@"Connecting to %@ on channel %d...",
                [addressField stringValue], [channelField intValue]]];
        
        // Send a Connect request to start the OBEX session.
        // You must send a Connect request before you send any other types of
        // requests.
        OBEXError status = [mClient sendConnectRequestWithHeaders:nil];
        if (status == kOBEXSuccess) {
            [self log:@"Sent 'Connect' request, waiting for response..."];
            [connectionProgress startAnimation:nil];
            [connectButton setEnabled:NO];
        } else {
            [self log:[NSString stringWithFormat:@"Connection error! (%d)", status]];
        }

    } else {
        [self log:@"Disconnecting..."];
        
        // Send a Disconnect requst to close the OBEX session.
        OBEXError status = [mClient sendDisconnectRequestWithHeaders:nil];
        if (status == kOBEXSuccess) {
            [self log:@"Sent 'Disconnect' request, waiting for response..."];
            [connectionProgress startAnimation:nil];
            [connectButton setEnabled:NO];        
        } else {
            [self log:[NSString stringWithFormat:@"Disconnection error! (%d)", status]];        
        }
    }
}

- (IBAction)chooseFile:(id)sender
{
	NSOpenPanel *openPanel = [NSOpenPanel openPanel];
	if ([openPanel runModalForTypes:nil] == NSOKButton)
		[filePathField setStringValue:[[openPanel filenames] objectAtIndex:0]];
}

- (IBAction)sendFile:(id)sender
{
    NSString *filePath = [filePathField stringValue];
    
    // Open a stream that will read from the local file. It doesn't have to be
    // retained because the client will retain it for the duration of the 
    // request.
    NSInputStream *fileInputStream = [NSInputStream inputStreamWithFileAtPath:filePath];
    [fileInputStream open]; // must be opened
    
    // Attach some request headers that tell the server the name and size of
    // the file that we are sending.
    BBMutableOBEXHeaderSet *headerSet = [BBMutableOBEXHeaderSet headerSet];
    [headerSet setValueForNameHeader:[filePath lastPathComponent]];
    NSNumber *fileSize = [self getSizeOfFile:filePath];
    if (fileSize) {    
        [headerSet setValueForLengthHeader:[fileSize unsignedIntValue]];
        [transferProgress setIndeterminate:NO];
        [transferProgress setMaxValue:[fileSize unsignedIntValue]];
        [self log:[NSString stringWithFormat:@"Sending file of size: %d",
                [fileSize unsignedIntValue]]];
    } else {
        [transferProgress setIndeterminate:YES];        
    }
    
    [self log:[NSString stringWithFormat:@"Sending Put request with file <%@>", 
        filePath]];
    
    // send the request
    OBEXError status = [mClient sendPutRequestWithHeaders:headerSet
                                           readFromStream:fileInputStream];
    if (status == kOBEXSuccess) {
        [self log:@"Sending file..."];
        [sendButton setEnabled:NO];
        [cancelButton setEnabled:YES];
        [transferProgress startAnimation:nil];
    } else {
        [self log:[NSString stringWithFormat:@"Put request error! (%d)", status]];        
    }
}

- (IBAction)cancelFileTransfer:(id)sender
{
    [self log:@"Attempting to cancel transfer..."];
    [mClient abortCurrentRequest];
}



#pragma mark -
#pragma mark BBBluetoothOBEXClient delegate methods

// The following delegate methods allow the BBBluetoothOBEXClient to notify 
// you when an OBEX event has occurred. You must wait for a notification
// that a request has finished before start the next request! For example,
// if you send a Connect request, you must wait for 
// client:didFinishConnectRequestWithError:response: to
// be called until you send another request. Otherwise, the OBEX server on the
// other end cannot process your requests correctly.


- (void)client:(BBBluetoothOBEXClient *)client
didFinishConnectRequestWithError:(OBEXError)error
      response:(BBOBEXResponse *)response
{
    if (error == kOBEXSuccess) {
        if ([response responseCode] == kOBEXResponseCodeSuccessWithFinalBit) {
            [self log:@"Connected."];
            [connectButton setTitle:@"Disconnect"];
        } else {
            [self log:[NSString stringWithFormat:@"Connect request refused (%@)",
                [response responseCodeDescription]]];
        }
    } else {
        [self log:[NSString stringWithFormat:@"Connect error! (%d)", error]];
    }
    
    [connectButton setEnabled:YES];
    [connectionProgress stopAnimation:nil];
}

- (void)client:(BBBluetoothOBEXClient *)client
didFinishDisconnectRequestWithError:(OBEXError)error 
      response:(BBOBEXResponse *)response
{
    if (error == kOBEXSuccess) {    
        if ([response responseCode] == kOBEXResponseCodeSuccessWithFinalBit) {
            [self log:@"Disconnected."];
        } else {
            [self log:[NSString stringWithFormat:@"Disconnect request refused (%@)",
                [response responseCodeDescription]]];
        }
    } else {
        [self log:[NSString stringWithFormat:@"Disconnect error! (%d)", error]];
    }        
    
    // close the baseband connection to the remote device 
    [[[client RFCOMMChannel] getDevice] closeConnection];
    [connectButton setTitle:@"Connect"];
    
    [connectButton setEnabled:YES];
    [connectionProgress stopAnimation:nil];
}

- (void)client:(BBBluetoothOBEXClient *)client
didFinishPutRequestForStream:(NSInputStream *)inputStream
         error:(OBEXError)error
      response:(BBOBEXResponse *)response
{
    if (error == kOBEXSuccess) {
        if ([response responseCode] == kOBEXResponseCodeSuccessWithFinalBit) {
            [self log:@"File sent successfully."];
        } else {
            [self log:[NSString stringWithFormat:@"Put request refused (%@)",
                [response responseCodeDescription]]];
        }        
    } else {
        [self log:[NSString stringWithFormat:@"Put error! (%d)", error]];
    }
    
    [inputStream close];
    
    [transferProgress stopAnimation:nil];
    [sendButton setEnabled:YES];
    [cancelButton setEnabled:NO];
}

- (void)client:(BBBluetoothOBEXClient *)client
  didSendDataOfLength:(unsigned)length
{
    [self log:[NSString stringWithFormat:@"Sent another %d bytes...", length]];
    [transferProgress incrementBy:length];
}

- (void)client:(BBBluetoothOBEXClient *)session
didAbortRequestWithStream:(NSStream *)stream
         error:(OBEXError)error
      response:(BBOBEXResponse *)response
{
    if (error == kOBEXSuccess) {
        if ([response responseCode] == kOBEXResponseCodeSuccessWithFinalBit) {
            [self log:@"File transfer cancelled."];
        } else {
            [self log:[NSString stringWithFormat:@"Abort request refused (%@)",
                [response responseCodeDescription]]];
        }            
    } else {
        [self log:[NSString stringWithFormat:@"Abort error! (%d)", error]];
    }

    [stream close];
    
    [transferProgress stopAnimation:nil];
    [transferProgress setDoubleValue:0];
    [sendButton setEnabled:YES];
    [cancelButton setEnabled:NO];    
}

- (void)dealloc
{
    [mClient release];
    [super dealloc];
}

@end
