import unreal
print("========================================")
print("HELLO FROM UNREAL PYTHON ENGINE AUTOMATION!")
print("========================================")

# Test loading an asset
cube = unreal.load_asset('/Engine/BasicShapes/Cube.Cube')
print(f"Loaded Cube: {cube}")

bp = unreal.load_asset('/Game/Blueprints/BP_Merchant')
print(f"Loaded BP_Merchant: {bp}")
