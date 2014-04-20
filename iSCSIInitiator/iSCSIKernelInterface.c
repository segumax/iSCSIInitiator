/**
 * @author		Nareg Sinenian
 * @file		iSCSIKernelInterface.c
 * @date		March 14, 2014
 * @version		1.0
 * @copyright	(c) 2013-2014 Nareg Sinenian. All rights reserved.
 */

#include "iSCSIKernelInterface.h"
#include "iSCSIPDUUser.h"
#include <IOKit/IOKitLib.h>

static io_service_t service;
static io_connect_t connection;

/** Opens a connection to the iSCSI initiator.  A connection must be
 *  successfully opened before any of the supporting functions below can be
 *  called. */
kern_return_t iSCSIKernelInitialize()
{
    kern_return_t kernResult;
 	
	// Create a dictionary to match iSCSIkext
	CFMutableDictionaryRef matchingDict = NULL;
	matchingDict = IOServiceMatching("com_NSinenian_iSCSIVirtualHBA");
    
    service = IOServiceGetMatchingService(kIOMasterPortDefault,matchingDict);
    
	// Check to see if the driver was found in the I/O registry
	if(service == IO_OBJECT_NULL)
	{
/*		NSAlert * alert = [[NSAlert alloc] init];
		
		
		[alert setMessageText:@"iSCSI driver has not been loaded"];
		[alert runModal];
*/
		return kIOReturnNotFound;
	}
    
	// Using the service handle, open a connection
	kernResult = IOServiceOpen(service,mach_task_self(),0,&connection);
	
	if(kernResult != kIOReturnSuccess) {
/*		NSAlert * alert = [[NSAlert alloc] init];
		
		
		[alert setMessageText:@"Couldnt open handle to service"];
		[alert runModal];
*/
	}
    return IOConnectCallScalarMethod(connection,kiSCSIOpenInitiator,0,0,0,0);
}

/** Closes a connection to the iSCSI initiator. */
kern_return_t iSCSIKernelCleanUp()
{
    kern_return_t kernResult =
        IOConnectCallScalarMethod(connection,kiSCSICloseInitiator,0,0,0,0);
    
	// Clean up (now that we have a connection we no longer need the object)
    IOObjectRelease(service);
    IOServiceClose(connection);
    
    return kernResult;
}

/** Allocates a new iSCSI session and returns a session qualifier ID.
 *  @return a valid session qualifier (part of the ISID, see RF3720) or
 *  0 if a new session could not be created. */
UInt16 iSCSIKernelCreateSession()
{
    // Expected output (a single number with the session qualifier)
    const UInt32 expOutputCnt = 1;
    
    // We must tell it how many outputs we are passing in (buffers for outputs)
    // or IOConnectCall... will fail!
    UInt32 outputCnt = 1;
    UInt64 output;
    
    // Obtain and return a session qualifier if we can successfully allocate
    // a session in the kernel
    if(IOConnectCallScalarMethod(connection,kiSCSICreateSession,
                                 0,0,&output,&outputCnt) == kIOReturnSuccess)
    {
        // We require at least one output integer
        if(outputCnt == expOutputCnt)
            return output;
    }
    
    // Else we couldnt allocate a session; quit
    return kiSCSIInvalidSessionId;
}

/** Releases an iSCSI session, including all connections associated with that
 *  session.
 *  @param sessionId the session qualifier part of the ISID. */
void iSCSIKernelReleaseSession(UInt16 sessionId)
{
    // Check parameters
    if(sessionId == kiSCSIInvalidSessionId)
        return;

    // Tell the kernel to drop this session and all of its related resources
    const UInt32 inputCnt = 1;
    UInt64 input = sessionId;
    
    IOConnectCallScalarMethod(connection,kiSCSIReleaseSession,&input,inputCnt,0,0);
}

/** Sets options associated with a particular connection.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param options the options to set.
 *  @return error code indicating result of operation. */
errno_t iSCSIKernelSetSessionOptions(UInt16 sessionId,
                                     iSCSISessionOptions * options)
{
    // Check parameters
    if(sessionId == kiSCSIInvalidSessionId || !options)
        return EINVAL;
    
    const UInt32 inputCnt = 1;
    const UInt64 input = sessionId;
    
    if(IOConnectCallMethod(connection,kiSCSISetSessionOptions,&input,inputCnt,
                           options,sizeof(struct iSCSISessionOptions),0,0,0,0) == kIOReturnSuccess)
    {
        return 0;
    }
    return EIO;
}

/** Gets options associated with a particular connection.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param options the options to get.  The user of this function is
 *  responsible for allocating and freeing the options struct.
 *  @return error code indicating result of operation. */
errno_t iSCSIKernelGetSessionOptions(UInt16 sessionId,
                                     iSCSISessionOptions * options)
{
    // Check parameters
    if(sessionId == kiSCSIInvalidSessionId || !options)
        return EINVAL;
    
    const UInt32 inputCnt = 1;
    const UInt64 input = sessionId;
    size_t optionsSize = sizeof(struct iSCSISessionOptions);

    if(IOConnectCallMethod(connection,kiSCSIGetSessionOptions,&input,inputCnt,0,0,0,0,
                           options,&optionsSize) == kIOReturnSuccess)
    {
        return 0;
    }
    return EIO;
}

/** Allocates a new iSCSI connection associated with the particular session.
 *  @param sessionId the session to create a new connection for.
 *  @param domain the IP domain (e.g., AF_INET or AF_INET6).
 *  @param targetAddress the BSD socket structure used to identify the target.
 *  @param hostAddress the BSD socket structure used to identify the host. This
 *  specifies the interface that the connection will be bound to.
 *  @return a connection ID, or 0 if a connection could not be created. */
UInt32 iSCSIKernelCreateConnection(UInt16 sessionId,
                                   int domain,
                                   const struct sockaddr * targetAddress,
                                   const struct sockaddr * hostAddress)
{
    // Check parameters
    if(!targetAddress || !hostAddress || sessionId == kiSCSIInvalidSessionId)
        return kiSCSIInvalidConnectionId;
    
    // Tell the kernel to drop this session and all of its related resources
    const UInt32 inputCnt = 2;
    const UInt64 inputs[] = {sessionId,domain};
    
    const UInt32 inputStructCnt = 2;
    const struct sockaddr addresses[] = {*targetAddress,*hostAddress};
    
    UInt64 output;
    UInt32 outputCnt = 1;
    
    if(IOConnectCallMethod(connection,kiSCSICreateConnection,inputs,inputCnt,
                           addresses,inputStructCnt*sizeof(struct sockaddr),
                           &output,&outputCnt,0,0) == kIOReturnSuccess)
    {
        return (UInt32)output;
    }

    // Else we couldn't allocate a connection; quit
    return kiSCSIInvalidConnectionId;
}

/** Frees a given
 iSCSI connection associated with a given session.
 *  The session should be logged out using the appropriate PDUs. */
void iSCSIKernelReleaseConnection(UInt16 sessionId,UInt32 connectionId)
{
    // Check parameters
    if(sessionId == kiSCSIInvalidSessionId || connectionId == kiSCSIInvalidConnectionId)
        return;

    // Tell kernel to drop this connection
    const UInt32 inputCnt = 2;
    UInt64 inputs[] = {sessionId,connectionId};
    
    IOConnectCallScalarMethod(connection,kiSCSIReleaseConnection,inputs,inputCnt,0,0);
}


/** Sends data over a kernel socket associated with iSCSI.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param connectionId the connection associated with the session.
 *  @param bhs the basic header segment to send over the connection.
 *  @param data the data segment of the PDU to send over the connection.
 *  @param length the length of the data block to send over the connection.
 *  @return error code indicating result of operation. */
errno_t iSCSIKernelSend(UInt16 sessionId,
                        UInt32 connectionId,
                        iSCSIPDUInitiatorBHS * bhs,
                        void * data,
                        size_t length)
{
    // Check parameters
    if(sessionId    == kiSCSIInvalidSessionId || !bhs || !data ||
       connectionId == kiSCSIInvalidConnectionId)
        return EINVAL;
    
    // Setup input scalar array
    const UInt32 inputCnt = 2;
    const UInt64 inputs[] = {sessionId, connectionId};
    
    const UInt32 expOutputCnt = 1;
    UInt32 outputCnt = 1;
    UInt64 output;
    
    // Call kernel method to send (buffer) bhs and then data
    if(IOConnectCallStructMethod(connection,kiSCSISendBHS,bhs,
            sizeof(iSCSIPDUInitiatorBHS),NULL,NULL) != kIOReturnSuccess)
    {
        return EINVAL;
    }
    
    if(IOConnectCallMethod(connection,kiSCSISendData,inputs,inputCnt,
            data,length,&output,&outputCnt,NULL,NULL) == kIOReturnSuccess)
    {
        if(outputCnt == expOutputCnt)
            return (errno_t)output;
    }
    
    // Return -1 as the BSD socket API normally would if the kernel call fails
    return EINVAL;
}

/** Receives data over a kernel socket associated with iSCSI.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param connectionId the connection associated with the session.
 *  @param bhs the basic header segment received over the connection.
 *  @param data the data segment of the PDU received over the connection.
 *  @param length the length of the data block received.
 *  @return error code indicating result of operation. */
errno_t iSCSIKernelRecv(UInt16 sessionId,
                        UInt32 connectionId,
                        iSCSIPDUTargetBHS * bhs,
                        void * * data,
                        size_t * length)
{
    // Check parameters
    if(sessionId == kiSCSIInvalidSessionId || connectionId == kiSCSIInvalidConnectionId || !bhs)
        return EINVAL;
    
    // Setup input scalar array
    const UInt32 inputCnt = 2;
    UInt64 inputs[] = {sessionId,connectionId};
    
    const UInt32 expOutputCnt = 1;
    UInt32 outputCnt = 1;
    UInt64 output;
    
    size_t bhsLength = sizeof(iSCSIPDUTargetBHS);

    // Call kernel method to determine how much data there is to receive
    // The inputs are the sesssion qualifier and connection ID
    // The output is the size of the buffer we need to allocate to hold the data
    kern_return_t kernResult;
    
    kernResult = IOConnectCallMethod(connection,kiSCSIRecvBHS,inputs,inputCnt,NULL,0,
                                     &output,&outputCnt,bhs,&bhsLength);
    
    if(kernResult != kIOReturnSuccess || outputCnt != expOutputCnt || output != 0)
        return EIO;
    
    // Determine how much data to allocate for the data buffer
    *length = iSCSIPDUGetPaddedDataSegmentLength((iSCSIPDUCommonBHS *)bhs);
    
    // If no data, were done at this point
    if(*length == 0)
        return 0;
    
    *data = iSCSIPDUDataCreate(*length);
        
    if(*data == NULL)
        return EIO;
    
    // Call kernel method to get data from a receive buffer
    if(IOConnectCallMethod(connection,kiSCSIRecvData,inputs,inputCnt,NULL,0,
                           &output,&outputCnt,*data,length) == kIOReturnSuccess)
    {
        if(outputCnt == expOutputCnt && output == 0)
            return 0;
    }
    
    // At this point we failed, free the temporary buffer and quit with error
    iSCSIPDUDataRelease(data);
    return EIO;
}


/** Sets options associated with a particular connection.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param connectionId the connection associated with the session.
 *  @param options the options to set.
 *  @return error code indicating result of operation. */
errno_t iSCSIKernelSetConnectionOptions(UInt16 sessionId,
                                        UInt32 connectionId,
                                        iSCSIConnectionOptions * options)
{
    // Check parameters
    if(sessionId == kiSCSIInvalidSessionId ||
       connectionId == kiSCSIInvalidConnectionId || !options)
        return EINVAL;
    
    const UInt32 inputCnt = 2;
    const UInt64 inputs[] = {sessionId,connectionId};
    
    if(IOConnectCallMethod(connection,kiSCSISetConnectionOptions,inputs,inputCnt,
                           options,sizeof(struct iSCSIConnectionOptions),0,0,0,0) == kIOReturnSuccess)
    {
        return 0;
    }
    return EIO;
}

/** Gets options associated with a particular connection.
 *  @param sessionId the qualifier part of the ISID (see RFC3720).
 *  @param connectionId the connection associated with the session.
 *  @param options the options to get.  The user of this function is
 *  responsible for allocating and freeing the options struct.
 *  @return error code indicating result of operation. */
errno_t iSCSIKernelGetConnectionOptions(UInt16 sessionId,
                                        UInt32 connectionId,
                                        iSCSIConnectionOptions * options)
{
    // Check parameters
    if(sessionId == kiSCSIInvalidSessionId ||
       connectionId     == kiSCSIInvalidConnectionId || !options)
        return EINVAL;
    
    const UInt32 inputCnt = 2;
    const UInt64 inputs[] = {sessionId,connectionId};

    size_t optionsSize = sizeof(struct iSCSIConnectionOptions);
    
    if(IOConnectCallMethod(connection,kiSCSIGetConnectionOptions,inputs,inputCnt,0,0,0,0,
                           options,&optionsSize) == kIOReturnSuccess)
    {
        return 0;
    }
    return EIO;
}

/** Gets the connection Id for any active connection associated with session.
 *  This function can be used when a connection is required to service a
 *  session.
 *  @param sessionId the session for which to retreive a connection.
 *  @return an active connection Id for the specified session. */
UInt32 iSCSIKernelGetActiveConnection(UInt16 sessionId)
{
    // Check parameters
    if(sessionId == kiSCSIInvalidSessionId)
        return kiSCSIInvalidConnectionId;
    
    const UInt32 inputCnt = 1;
    const UInt64 input = sessionId;
    
    const UInt32 expOutputCnt = 1;
    UInt32 outputCnt = 1;
    UInt64 output;
    
    if(IOConnectCallScalarMethod(connection,kiSCSIGetActiveConnection,&input,
                                 inputCnt,&output,&outputCnt) == kIOReturnSuccess)
    {
        if(outputCnt == expOutputCnt)
            return (UInt32)output;
    }
    
    return kiSCSIInvalidConnectionId;
}

