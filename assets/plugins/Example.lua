local Plugin = require("Plugin")

Example = Plugin:new()

--- Required for the plugin to work
Example.name = "Example plugin"
Example.description = "Example plugin description"
--- Optional
Example.author = "Example author"
Example.version = "1.0.0"
Example.license = "MIT"
Example.url = "https://github.com/example/example"

function Example:init()
    print("Example plugin initialized")
end
function Example:OnBeforePromptFormat(prompt)
    print("Plugins dir: " .. self.pluginsDir)
    return "[Custom] " .. prompt
end

function Example:OnAlias(alias_key, alias_value)
    print("Alias key: " .. alias_key)  -- Várhatóan sztring
    print("Alias value: " .. alias_value)  -- Várhatóan sztring
    print("Plugins dir: " .. self.get("pluginsDir"))
    return ""
end

return Example