local avl = require "utils.avl"
local tap = require("tap")

local test = tap.test("tools-utils-avl")
test:plan(7)

local function traverse(node, result)
  if node ~= nil then
    table.insert(result, node.key)
    traverse(node.left, result)
    traverse(node.right, result)
  end
  return result
end

local function batch_insert(root, values) 
  for i = 1, #values do
    root = avl.insert(root, values[i])
  end

  return root
end

local function compare(arr1, arr2)
	for i, v in pairs(arr1) do
    if v ~= arr2[i] then
      return false
    end
  end
  return true
end

-- 1L rotation test.
local root = batch_insert(nil, {1, 2, 3})
test:ok(compare(traverse(root, {}), {2, 1, 3}))

-- 1R rotation test.
root = batch_insert(nil, {3, 2, 1})
test:ok(compare(traverse(root, {}), {2, 1, 3}))

-- 2L rotation test.
root = batch_insert(nil, {1, 3, 2})
test:ok(compare(traverse(root, {}), {2, 1, 3}))

-- 2R rotation test.
root = batch_insert(nil, {3, 1, 2})
test:ok(compare(traverse(root, {}), {2, 1, 3}))

-- Exact upper bound.
test:ok(avl.upper_bound(root, 1) == 1)

-- No upper bound.
test:ok(avl.upper_bound(root, -10) == nil)

-- Not exact upper bound.
test:ok(avl.upper_bound(root, 2.75) == 2)



