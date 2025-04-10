Example = require("BasePlugin")

--- Required for the plugin to work
Example.name = "Example plugin"
Example.description = "Example plugin description"
--- Optional
Example.author = "fszontagh"
Example.version = "1.0.0"
Example.license = "MIT"
Example.url = "https://github.com/fszontagh/simpleshell"

function Example:init()
  -- print(Example.name .." plugin initialized")
  -- print("BasePlugins dir: " .. Example:get("pluginsDir"))
end
--function Example:OnBeforePromptFormat(prompt)
--    return "$"
--end

function Example:OnAlias(alias_key, alias_value)
    print("Alias key: " .. alias_key)
    print("Alias value: " .. alias_value)
    print("BasePlugins dir: " .. Example:get("pluginsDir"))
    return ""
end

function Example:OnCommand(command, fullCommand)
    if command == "example" then
        print("Example command executed")

    end
return ""
end

return Example