local io = require("io")
local http = require("socket.http")
local json = require "json"
local ltn12 = require("ltn12")
local socket = require("socket") -- stream módhoz

ollama = require("BasePlugin")

--- Required for the plugin to work
ollama.name = "Ollama plugin"
ollama.description = "A plugin to communicate with ollama"
--- Optional
ollama.author = "fszontagh"
ollama.version = "1.0.0"
ollama.license = "MIT"
ollama.model = "moondream:latest"
ollama.message = {role = "user", content = "Hello, how are you?"}
ollama.history = {}
TIMEOUT = 60

local endpoints = {
    ["generate"] = "/api/generate",
    ["chat"] = "/api/chat",
    ["pull"] = "/api/pull",
    ["push"] = "/api/push",
    ["list"] = "/api/list",
    ["create"] = "/api/create"
}

function ollama:init()
    ollama.host = ollama.getConfigValue("host", "localhost")
    ollama.port = ollama.getConfigValue("port", "11434")
    ollama.model = ollama.getConfigValue("model", "moondream:latest")
    ollama.ollama_url = "http://" .. ollama.host .. ":" .. ollama.port

    ollama.setConfigValue("host", ollama.host)
    ollama.setConfigValue("port", ollama.port)
    ollama.setConfigValue("model", ollama.model)
    if ollama.ollama_url:match("^https://") then
        print("[stream] HTTPS not supported")
        streaming_enabled = false
    end

end

function ollama:pullModel(model)
    -- optional: implement pullModel if needed
end

function ollama:streamRequest(endpoint, data)
    local client = assert(socket.tcp())
    client:settimeout(1)
    assert(client:connect(ollama.host, tonumber(ollama.port)))

    local json_data = json.encode(data)
    local request = table.concat({
        "POST " .. endpoint .. " HTTP/1.1",
        "Host: " .. ollama.host .. ":" .. ollama.port,
        "Content-Type: application/json", "Content-Length: " .. #json_data, "",
        json_data
    }, "\r\n")

    assert(client:send(request))

    -- Fejlécek átugrása
    while true do
        local line, err = client:receive("*l")
        if not line then break end
        if line == "" or line == "\r" or line == "\r\n" then break end
    end

    -- Streamelt JSON chunk-ok
    while true do
        local line = client:receive("*l")
        if not line then break end

        if #line > 0 then
            local ok, chunk = pcall(json.decode, line)
            if ok and chunk then
                if chunk.response then
                    io.write(chunk.response)
                    io.flush()
                end
                if chunk.done then break end
            end
        end
    end

    client:close()
end

function split_lines(str)
    local t = {}
    for line in string.gmatch(str, "([^\n]+)") do table.insert(t, line) end
    return t
end

function ollama:OnCommand(command, args)
    if command == ">" or command == ">>" then
        local endpoint = endpoints["chat"]
        local prompt = table.concat(args, " ")
        local streaming_enabled = (command == ">>")

        table.insert(ollama.history, {role = "user", content = prompt})
        local data = {
            model = ollama.model,
            messages = ollama.history,
            stream = streaming_enabled
        }

        if streaming_enabled then
            self:streamRequest(endpoint, data)
        else
            local jdata = json.encode(data)
            local response_body = {}
            local res, code = http.request {
                url = ollama.ollama_url .. endpoint,
                method = "POST",
                sink = ltn12.sink.table(response_body),
                source = ltn12.source.string(jdata),
                headers = {
                    ["Content-Type"] = "application/json",
                    ["Content-Length"] = tostring(#jdata)
                }
            }
            if code ~= 200 then
                print("Error (" .. tostring(code) .. "): " .. tostring(res))
            else

                local response = json.decode(table.concat(response_body, ""))
                local lines = split_lines(response.message.content)
                for _, line in ipairs(lines) do
                    print("> " .. line)
                end
                table.insert(ollama.history, response.message)
            end
        end

        return false
    end

    return true
end

return ollama
