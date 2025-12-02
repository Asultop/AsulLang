#ifndef ASUL_ASYNC_H
#define ASUL_ASYNC_H

#include "AsulRuntime.h"
#include <functional>
#include <memory>

namespace asul {

/**
 * AsulAsync - Interface for async operations in external packages
 * 
 * This interface allows external packages (std.network, std.io, std.encoding)
 * to perform asynchronous operations without directly depending on the Interpreter.
 * 
 * Usage in packages:
 *   void registerMyPackage(Interpreter& interp) {
 *       AsulAsync& async = interp.getAsyncInterface();
 *       
 *       // Create a promise
 *       auto promise = async.createPromise();
 *       
 *       // Post async work
 *       async.postTask([promise, &async]() {
 *           // ... do async work ...
 *           async.resolve(promise, result);  // or async.reject(promise, error);
 *       });
 *       
 *       return Value{promise};
 *   }
 */
class AsulAsync {
public:
	virtual ~AsulAsync() = default;
	
	// Create a new promise that will be associated with this async context
	virtual std::shared_ptr<PromiseState> createPromise() = 0;
	
	// Resolve a promise with a value (rejected = false)
	virtual void resolve(std::shared_ptr<PromiseState> promise, const Value& value) = 0;
	
	// Reject a promise with an error (rejected = true)
	virtual void reject(std::shared_ptr<PromiseState> promise, const Value& error) = 0;
	
	// Post a task to the event loop for async execution
	virtual void postTask(std::function<void()> task) = 0;
	
	// Settle a promise (low-level, prefer resolve/reject)
	virtual void settlePromise(std::shared_ptr<PromiseState> promise, bool rejected, const Value& result) = 0;
	
	// Dispatch promise callbacks after settlement
	virtual void dispatchPromiseCallbacks(std::shared_ptr<PromiseState> promise) = 0;
};

} // namespace asul

#endif // ASUL_ASYNC_H
