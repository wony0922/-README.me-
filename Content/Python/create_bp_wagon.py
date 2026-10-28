import unreal

ASSET_PATH = "/Game/Blueprints/BP_Wagon"
PACKAGE_PATH = "/Game/Blueprints"
VARIABLE_NAME = "MaxCapacity"


def run():
    if not unreal.EditorAssetLibrary.does_directory_exist(PACKAGE_PATH):
        unreal.EditorAssetLibrary.make_directory(PACKAGE_PATH)

    asset_exists = unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH)
    asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH) if asset_exists else None
    if asset is None:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.Actor)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "BP_Wagon", PACKAGE_PATH, unreal.Blueprint, factory
        )
        if asset is None:
            raise RuntimeError("Failed to create " + ASSET_PATH)
        unreal.log("Created " + ASSET_PATH)
    elif not isinstance(asset, unreal.Blueprint):
        raise TypeError(ASSET_PATH + " exists but is not a Blueprint asset")
    else:
        unreal.log("Using existing Blueprint " + ASSET_PATH)

    variable_names = unreal.BlueprintEditorLibrary.list_member_variable_names(
        asset, include_inherited_members=False
    )
    if VARIABLE_NAME not in variable_names:
        int_pin_type = unreal.BlueprintEditorLibrary.get_basic_type_by_name("int")
        if not unreal.BlueprintEditorLibrary.add_member_variable(
            asset, VARIABLE_NAME, int_pin_type
        ):
            raise RuntimeError("Failed to add Integer variable " + VARIABLE_NAME)
        unreal.log("Added Integer variable " + VARIABLE_NAME)

    variable_type = unreal.BlueprintEditorLibrary.get_member_variable_type(asset, VARIABLE_NAME)
    if variable_type is None:
        raise TypeError(VARIABLE_NAME + " exists but is not an Integer variable")

    unreal.log("Verified variable type: " + str(variable_type))

    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError("Failed to save " + ASSET_PATH)

    unreal.log("SUCCESS: " + ASSET_PATH + " contains Integer variable " + VARIABLE_NAME)


run()
