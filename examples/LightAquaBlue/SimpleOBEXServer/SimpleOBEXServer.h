/* SimpleOBEXServer */

#import <Cocoa/Cocoa.h>
#import <IOBluetooth/Bluetooth.h>

@class BBBluetoothOBEXServer;
@class IOBluetoothUserNotification;

@interface SimpleOBEXServer : NSObject
{
    BluetoothSDPServiceRecordHandle mServiceRecordHandle;
    IOBluetoothUserNotification *mChannelNotification;
    NSMutableDictionary *mOBEXServers;
    
    IBOutlet id logView;
    IBOutlet id serverDirectoryField;
    IBOutlet id serviceNameField;
    IBOutlet id startButton;   
    
    NSMutableArray *mConnections;
    IBOutlet id mConnectionsController;
}

- (IBAction)startOrStopServer:(id)sender;
@end
