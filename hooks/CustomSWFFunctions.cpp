#include "CustomSWFFunctions.h"

namespace CustomSWFFunctions {
    Logger& l = Logger::Instance();
    static std::unordered_map<uint16_t, CustomSWFFunction> customFunctions;

    void HelloWorld(int paramCount, SWFArgument** swfArgs, SWFReturn* swfReturn) {
        l.Get()->info("HelloWorld function called with {} parameters", paramCount);

        if (paramCount < 1) {
            l.Get()->error("HelloWorld called with 0 params, required 1!");
            l.Get()->flush();

            if (swfReturn) {
                swfReturn->SetFailure();
            }
        }
        else {
            auto param = SWFArgumentReader::GetValueAsString(swfArgs[0]);
            l.Get()->info("HelloWorld called with param: {}", param);
            l.Get()->flush();

            // Make sure to set success to properly return a value
            if (swfReturn) {
                swfReturn->SetBooleanSuccess(true);
            }
        }
    }

    bool IsCustomFunction(uint16_t id) {
        return id >= 10000;
    }

    bool RegisterCustomFunction(SWFFunctionID functionId, CustomSWFFunction function) {
        uint16_t id = ToValue(functionId);
        if (!IsCustomFunction(id)) {
            l.Get()->error("Attempted to register invalid custom function ID: {}", id);
            l.Get()->flush();
            return false;
        }

        // Add additional logging to verify the ID being used
        l.Get()->info("Registering custom function handler with ID: {}", id);

        customFunctions[id] = function;
        l.Get()->info("Successfully registered custom function handler with ID: {}", id);
        l.Get()->flush();
        return true;
    }

    std::vector<uint16_t> GetRegisteredFunctionIds() {
        std::vector<uint16_t> ids;
        for (const auto& pair : customFunctions) {
            ids.push_back(pair.first);
        }
        return ids;
    }

    bool HandleCustomCall(uint16_t functionId, void* thisPtr, int swfContext,
        int paramCount, SWFArgument** swfArgs, SWFReturn* swfReturn, uint32_t callbackPtr) {

        // Add logging to show what functionId is being looked up
        l.Get()->info("HandleCustomCall looking for function ID: {}", functionId);

        auto it = customFunctions.find(functionId);
        if (it == customFunctions.end()) {
            // Log all registered function IDs to help debug
            l.Get()->warn("No handler found for custom function ID: {}", functionId);
            l.Get()->info("Currently registered function handlers:");
            for (const auto& pair : customFunctions) {
                l.Get()->info("  - ID: {}", pair.first);
            }
            l.Get()->flush();
            return false;
        }

        try {
            l.Get()->info("Calling custom function with ID: {}", functionId);
            l.Get()->flush();

            // Call the function
            it->second(paramCount, swfArgs, swfReturn);

            // Verify the function was called successfully
            l.Get()->info("Custom function call completed");
            return true;
        }
        catch (const std::exception& e) {
            l.Get()->error("Exception in custom function {}: {}", functionId, e.what());
            l.Get()->flush();
            if (swfReturn) {
                swfReturn->SetFailure();
            }
            return true;
        }
    }

    void Initialize() {
        l.Get()->info("Initializing CustomSWFFunctions");

        // Get the ID value first for logging
        uint16_t helloWorldId = ToValue(SWFFunctionID::HelloWorld);
        l.Get()->info("About to register HelloWorld handler with ID: {}", helloWorldId);

        // Register the HelloWorld function
        bool success = RegisterCustomFunction(SWFFunctionID::HelloWorld, HelloWorld);

        if (success) {
            l.Get()->info("Custom SWF functions system initialized successfully");
        }
        else {
            l.Get()->error("Failed to initialize custom SWF functions system");
        }

        l.Get()->flush();
    }
}