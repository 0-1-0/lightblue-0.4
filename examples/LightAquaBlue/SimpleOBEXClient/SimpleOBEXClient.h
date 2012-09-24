/* SimpleOBEXClient */

#import <Cocoa/Cocoa.h>

@class BBBluetoothOBEXClient;

@interface SimpleOBEXClient : NSObject
{
    BBBluetoothOBEXClient *mClient;
    
    IBOutlet id addressField;
    IBOutlet id cancelButton;
    IBOutlet id channelField;
    IBOutlet id connectButton;
    IBOutlet id connectionProgress;
    IBOutlet id filePathField;
    IBOutlet id logTextView;
    IBOutlet id sendButton;
    IBOutlet id transferProgress;
}
- (IBAction)findService:(id)sender;
- (IBAction)connectOrDisconnect:(id)sender;
- (IBAction)chooseFile:(id)sender;
- (IBAction)sendFile:(id)sender;
- (IBAction)cancelFileTransfer:(id)sender;
@end
