//
//	SimpleOBEXServer.m
//
//	This shows how BBBluetoothOBEXServer can be used to run a simple OBEX 
//	server that allows clients to send and retrieve files.
//
//	To receive files through this server, you'll need to get the remote device 
//	to specifically send them to the channel that's being used by this OBEX 
//	service -- that is, the channel number that's displayed when this example
//  prints the "Listening for connections on channel X" message.
//
//	If you can't specify a channel when sending files from the remote device --
//  for example, the Mac OS X "Send File..." Bluetooth utility only lets you 
//  select a device, and not the channel -- this probably means the device is
//  just sending it to the first appropriate OBEX service that it finds. So if 
//  you switch off all other OBEX services, e.g. the Mac OS X built-in OBEX 
//  services, this demo's OBEX service should automatically receive the files. 
//
//  To switch off the built-in OBEX services on Mac OS 10.5 go to 
//  System Preferences -> Sharing and uncheck the "Bluetooth Sharing" checkbox.
//	This disables the built-in OBEX Object Push and OBEX File Transfer services.
//
//  On Mac OS 10.4, go to System Preferences -> Bluetooth and click the
//  "Sharing" tab. Uncheck the "On" checkboxes for the "Bluetooth File Transfer"
//  and "Bluetooth File Exchange" services. 
//
//	On Mac OS 10.3, go to System Preferences -> Bluetooth, and go to the "File
//  Exchange"  tab. For "When receiving items", select "Refuse all", and also
//  uncheck "Allow other devices to browse files on this computer". 
//
//  If the device that's sending files is a Mac, then you can can specify the
//  channel ID programmatically: the included SimpleOBEXClient example, as well 
//  as Apple's OBEXSample (in /Developer/Examples/Bluetooth) and the 
//  IOBluetooth OBEXFileTransferServices class all let you connect to a 
//  specific channel when connecting to an OBEX service.
//

#import "SimpleOBEXServer.h"
#include <LightAquaBlue/LightAquaBlue.h>

#include <IOBluetooth/objc/IOBluetoothRFCOMMChannel.h>
#include <IOBluetooth/IOBluetoothUtilities.h>


@implementation SimpleOBEXServer

- (id)init
{
    self = [super init];
    mOBEXServers = [[NSMutableDictionary alloc] initWithCapacity:0];
    mConnections = [[NSMutableArray alloc] initWithCapacity:0];
    mServiceRecordHandle = 0;
    return self;
}

- (void)awakeFromNib
{
    [serverDirectoryField setStringValue:[[NSFileManager defaultManager] currentDirectoryPath]];
    [[logView window] center];
    //[BBBluetoothOBEXServer setDebug:YES];
}

- (void)log:(NSString *)text
{
    [logView insertText:text];
    [logView insertText:@"\n"];    
}

- (NSNumber *)getSizeOfFile:(NSString *)filePath
{
    NSFileManager *fileManager = [NSFileManager defaultManager];
    NSDictionary *fileAttributes = [fileManager fileAttributesAtPath:filePath traverseLink:YES];
    if (fileAttributes != nil)
        return [fileAttributes objectForKey:NSFileSize];
    return nil;
}

//
// Starts or stops the server.
//
- (IBAction)startOrStopServer:(id)sender
{
    if (!mChannelNotification) {
        [self log:@"Starting server..."];
        
        // Advertise an OBEX service so that other devices can find us when
        // they look for available services on our local Bluetooth device.
        BluetoothRFCOMMChannelID serverChannelID;
        BluetoothSDPServiceRecordHandle serviceRecordHandle;
        IOReturn result = 
            [BBServiceAdvertiser addRFCOMMServiceDictionary:[BBServiceAdvertiser objectPushProfileDictionary]
                                                   withName:[serviceNameField stringValue]
                                                       UUID:nil
                                                  channelID:&serverChannelID
                                        serviceRecordHandle:&serviceRecordHandle];
        if (result != kOBEXSuccess) {
            [self log:[NSString stringWithFormat:@"Error advertising service (%d)", result]];
            return;
        }
        
        // keep the service record handle so the service can be unregistered 
        // later
        mServiceRecordHandle = serviceRecordHandle;
        
        // Tell IOBluetoothRFCOMMChannel to call channelOpened:channel: 
        // when a client connects on <serverChannelID>, which is the channel
        // ID on which the OBEX service was advertised.
        mChannelNotification = 
            [IOBluetoothRFCOMMChannel registerForChannelOpenNotifications:self
                                                                 selector:@selector(channelOpened:channel:)
                                                            withChannelID:serverChannelID
                                                                direction:kIOBluetoothUserNotificationChannelDirectionIncoming];
        [mChannelNotification retain];
        [self log:[NSString stringWithFormat:@"Listening for connections on channel %d", 
            serverChannelID]];
        
        [startButton setTitle:@"Stop server"];
        
    } else {
        [self log:@"Stopping server..."];
        
        // Stop listening for RFCOMM client connections, and remove the
        // service that was previously advertised.
        [mChannelNotification unregister];
        [mChannelNotification release];
        mChannelNotification = nil;
        [BBServiceAdvertiser removeService:mServiceRecordHandle];
        mServiceRecordHandle = 0;
        
        [self log:@"Server stopped."];
        [startButton setTitle:@"Start server"];        
    }
}

//
// Called by IOBluetoothRFCOMMChannel when a RFCOMM client has connected.
// The provided <channel> is the newly opened RFCOMM channel.
// 
- (void)channelOpened:(IOBluetoothUserNotification *)notification
              channel:(IOBluetoothRFCOMMChannel *)channel
{
    NSString *remoteAddress = [[channel getDevice] getAddressString];
    [mConnectionsController addObject:remoteAddress];
    [self log:[NSString stringWithFormat:@"=> Client connected from %@",
        remoteAddress]];
    
    // create a BBBluetoothOBEXServer that will run on this new RFCOMM channel
    BBBluetoothOBEXServer *server = 
        [BBBluetoothOBEXServer serverWithIncomingRFCOMMChannel:channel
                                                      delegate:self];
    
    // Tell IOBluetoothRFCOMMChannel to call channelClosed:channel: when this
    // channel is closed, and add the new server to a dictionary so the object 
    // can be retained until the channel is closed.
    [channel registerForChannelCloseNotification:self selector:@selector(channelClosed:channel:)];
    [mOBEXServers setObject:server forKey:channel];
    
    // start the server
    [server run];
}

//
// Called by IOBluetoothRFCOMMChannel when a RFCOMM channel is closed (i.e when
// a client has disconnected).
// 
- (void)channelClosed:(IOBluetoothUserNotification *)notification
              channel:(IOBluetoothRFCOMMChannel *)channel
{
    NSString *remoteAddress = [[channel getDevice] getAddressString];
    [mConnectionsController removeObject:remoteAddress];
    [self log:[NSString stringWithFormat:@"=> Connection closed from %@",
        remoteAddress]];
    
    // the BBBluetoothOBEXServer running on this particular channel can now
    // be released
    [mOBEXServers removeObjectForKey:channel];
}



#pragma mark -
#pragma mark BBBluetoothOBEXServer delegate methods

// The following delegate methods are called by the BBBluetoothOBEXServer object
// when an OBEX server event occurs. This is where you can actually respond and 
// perform operations when a client connects, sends files, etc. 
//
// For the sake of simplicity, the delegate methods relating to SetPath and 
// Put-Delete requests have not been implemented here, which means that this 
// server won't support these particular requests.


//
// Called when an error occurs on the server.
//
- (void)server:(BBBluetoothOBEXServer *)server
 errorOccurred:(OBEXError)error
   description:(NSString *)description
{
    [self log:@"----------"];
    [self log:[NSString stringWithFormat:@"Server error: %@ (%d)", description, 
            error]];
    [self log:@"----------"];
}

//
// Called when a Connect request is received.
//
- (BOOL)server:(BBBluetoothOBEXServer *)server
shouldHandleConnectRequest:(BBOBEXHeaderSet *)requestHeaders
{
    [self log:@"Got CONNECT request"];
    return YES;
}

//
// Called when a Connect request is finished.
//
- (void)serverDidHandleConnectRequest:(BBBluetoothOBEXServer *)server
{
    [self log:@"Finished handling CONNECT request"];
}

//
// Called when a Disconnect request is received.
//
- (BOOL)server:(BBBluetoothOBEXServer *)server
shouldHandleDisconnectRequest:(BBOBEXHeaderSet *)requestHeaders
{
    [self log:@"Got DISCONNECT request"];
    return YES;    
}

//
// Called when a Disconnect request is finished.
//
- (void)serverDidHandleDisconnectRequest:(BBBluetoothOBEXServer *)server
{
    [self log:@"Finished handling DISCONNECT request"];
}

//
// Called when a Put request is received.
//
- (NSOutputStream *)server:(BBBluetoothOBEXServer *)server
    shouldHandlePutRequest:(BBOBEXHeaderSet *)requestHeaders
{
    // The client should have sent some information about the file, such as
    // its name and length.
    NSString *incomingFileName = [requestHeaders valueForNameHeader];
    if (!incomingFileName)
        incomingFileName = @"unnamed_file";
    [self log:[NSString stringWithFormat:@"Got PUT request, client wants to send file '%@'", 
        incomingFileName]];

    // See if the client told us how big the file is.
    // kOBEXHeaderIDLength and other header IDs are defined in OBEX.h in 
    // IOBluetooth.framework.
    if ([requestHeaders containsValueForHeader:kOBEXHeaderIDLength]) {
        [self log:[NSString stringWithFormat:@"Incoming file is %d bytes",
            [requestHeaders valueForLengthHeader]]];
    }
    
    // IOBluetoothGetUniqueFileNameAndPath() is a useful function in 
    // <IOBluetooth/IOBluetoothUtilities.h> for creating a unique file name.
    NSString *filePath = IOBluetoothGetUniqueFileNameAndPath(incomingFileName, 
             [serverDirectoryField stringValue]);
    [self log:[NSString stringWithFormat:@"File will be saved to <%@>", filePath]];
    
    // Now open a stream that will write to the file. It doesn't have to be
    // retained because the server will retain it for the duration of the 
    // request.
    NSOutputStream *stream = [NSOutputStream outputStreamToFileAtPath:filePath 
                                                               append:NO];
    [stream open];  // must open stream or else it can't be used!
    return stream;
}

//
// Called each time data is received during a Put request.
//
- (BOOL)server:(BBBluetoothOBEXServer *)server
didReceiveDataOfLength:(unsigned)length
  isLastPacket:(BOOL)isLastPacket
{
    [self log:[NSString stringWithFormat:@"Got another %d bytes", length]];
    return YES;
}

//
// Called when a Put request is finished.
//
- (void)server:(BBBluetoothOBEXServer *)server
didHandlePutRequestForStream:(NSOutputStream *)outputStream
    requestWasAborted:(BOOL)aborted
{
    [self log:@"Finished PUT request"];
    [outputStream close];    
}

//
// Called when a Get request is received.
//
- (NSInputStream *)server:(BBBluetoothOBEXServer *)server
   shouldHandleGetRequest:(BBOBEXHeaderSet *)requestHeaders
{
    // See what file the client wants to retrieve.
    NSString *fileName = [requestHeaders valueForNameHeader];    
    if (!fileName) {
        [self log:@"Got GET request but client didn't send a filename, refusing request..."];
        return NO;
    }    
        
    [self log:[NSString stringWithFormat:@"Got GET request, client wants to get file '%@'", 
        fileName]];    
    
    NSString *filePath = [[serverDirectoryField stringValue] stringByAppendingPathComponent:fileName];
    
    if (![[NSFileManager defaultManager] fileExistsAtPath:filePath]) {
        [self log:[NSString stringWithFormat:@"Cannot find file <%@>, refusing request...",
            filePath]];
        
        // You can use setResponseCodeForCurrentRequest: to set a more specific
        // response; otherwise if you return NO, the default response will
        // be 'Forbidden'.
        [server setResponseCodeForCurrentRequest:kOBEXResponseCodeNotFoundWithFinalBit];
        return NO;
    }
    
    // Use the response headers to inform the client of the file's size so it
    // knows how much data is expected.
    NSNumber *fileSize = [self getSizeOfFile:filePath];
    if (fileSize) {
        BBMutableOBEXHeaderSet *responseHeaders = [BBMutableOBEXHeaderSet headerSet];
        [responseHeaders setValueForLengthHeader:[fileSize unsignedIntValue]];
        [server addResponseHeadersForCurrentRequest:responseHeaders];
    }
        
    // Now open a stream that will read from the file. It doesn't have to be
    // retained because the server will retain it for the duration of the 
    // request.        
    NSInputStream *stream = [NSInputStream inputStreamWithFileAtPath:filePath];
    [stream open];  // must open stream or it can't be used!
    return stream;    
}

//
// Called each time data is sent for a Get request.
//
- (void)server:(BBBluetoothOBEXServer *)server
didSendDataOfLength:(unsigned)length
{
    [self log:[NSString stringWithFormat:@"Sent another %d bytes", length]];
}

//
// Called when a Get request is finished.
//
- (void)server:(BBBluetoothOBEXServer *)server
didHandleGetRequestForStream:(NSInputStream *)inputStream
    requestWasAborted:(BOOL)aborted
{
    [self log:@"Finished GET request"];
    [inputStream close];
}


- (void)dealloc
{
    if (mServiceRecordHandle != 0)
        [BBServiceAdvertiser removeService:mServiceRecordHandle];
    
    [mChannelNotification release];
    [mOBEXServers release];
    [super dealloc];
}

@end
