-- Every assert must hold inside the sandbox.
function on_start()
    assert(os == nil, "os must be removed")
    assert(loadstring == nil, "loadstring must be removed")
    assert(not pcall(function() math.floor = nil end), "the standard library must be read-only")
    assert(not pcall(function() zaro.get = nil end), "zaro must be read-only")
end
