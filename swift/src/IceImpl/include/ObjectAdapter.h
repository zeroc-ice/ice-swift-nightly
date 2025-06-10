// Copyright (c) ZeroC, Inc.
#import "LocalObject.h"

@class ICECommunicator;
@class ICEObjectPrx;
@class ICEEndpoint;
@class ICEConnection;
@protocol ICEDispatchAdapter;

NS_ASSUME_NONNULL_BEGIN

ICEIMPL_API @interface ICEObjectAdapter : ICELocalObject
- (NSString*)getName;
- (ICECommunicator*)getCommunicator;
- (BOOL)activate:(NSError* _Nullable* _Nullable)error;
- (void)hold;
- (void)waitForHold;
- (void)deactivate;
- (void)waitForDeactivate;
- (BOOL)isDeactivated;
- (void)destroy;
- (nullable ICEObjectPrx*)createProxy:(NSString*)name
                             category:(NSString*)category
                                error:(NSError* _Nullable* _Nullable)error NS_SWIFT_NAME(createProxy(name:category:));
- (nullable ICEObjectPrx*)createDirectProxy:(NSString*)name
                                   category:(NSString*)category
                                      error:(NSError* _Nullable* _Nullable)error
    NS_SWIFT_NAME(createDirectProxy(name:category:));
- (nullable ICEObjectPrx*)createIndirectProxy:(NSString*)name
                                     category:(NSString*)category
                                        error:(NSError* _Nullable* _Nullable)error
    NS_SWIFT_NAME(createIndirectProxy(name:category:));
- (void)setLocator:(ICEObjectPrx* _Nullable)locator;
- (nullable ICEObjectPrx*)getLocator;
- (NSArray<ICEEndpoint*>*)getEndpoints;
- (NSArray<ICEEndpoint*>*)getPublishedEndpoints;
- (BOOL)setPublishedEndpoints:(NSArray<ICEEndpoint*>*)newEndpoints error:(NSError* _Nullable* _Nullable)error;
- (void)registerDispatchAdapter:(id<ICEDispatchAdapter>)dispatchAdapter NS_SWIFT_NAME(registerDispatchAdapter(_:));
@end

#ifdef __cplusplus

@interface ICEObjectAdapter ()
@property(nonatomic, readonly) std::shared_ptr<Ice::ObjectAdapter> objectAdapter;
@end

#endif

NS_ASSUME_NONNULL_END
