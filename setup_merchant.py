import unreal

print("=== Setting up BP_Merchant ===")

# 1. Import Texture
tex_source = "C:/Unreal Projects/Ore Deal/-README.me-/SourceArt/UI/Store/T_Store_WagonButton.png"
tex_dest_path = "/Game/MaterialTraders/UI/Store"
tex_name = "T_Store_WagonButton"

task = unreal.AssetImportTask()
task.set_editor_property('filename', tex_source)
task.set_editor_property('destination_path', tex_dest_path)
task.set_editor_property('destination_name', tex_name)
task.set_editor_property('replace_existing', True)
task.set_editor_property('automated', True)
task.set_editor_property('save', True)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
tex = unreal.load_asset(f"{tex_dest_path}/{tex_name}")
print(f"Imported Texture: {tex}")

# 2. Create Material
mat_dest_path = "/Game/MaterialTraders/UI/Store"
mat_name = "M_Store_WagonButton"

existing_mat = unreal.load_asset(f"{mat_dest_path}/{mat_name}")
if not existing_mat:
    mat_factory = unreal.MaterialFactoryNew()
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(mat_name, mat_dest_path, unreal.Material, mat_factory)
else:
    mat = existing_mat

if mat and tex:
    # Setup material with texture sample
    unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)
    tex_sample = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -300, 0)
    tex_sample.set_editor_property('texture', tex)
    unreal.MaterialEditingLibrary.connect_material_property(tex_sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_asset(f"{mat_dest_path}/{mat_name}")
    print(f"Created and saved Material: {mat}")

print("=== Done Step 1 & 2 ===")
