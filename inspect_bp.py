import unreal

print("--- SubobjectDataSubsystem ---")
try:
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    print("Found SubobjectDataSubsystem:", subsystem)
except Exception as e:
    print("SubobjectDataSubsystem error:", e)

bp = unreal.load_asset('/Game/Blueprints/BP_Merchant')
print("BP_Merchant class:", bp.get_class().get_name())
print("BP parent class:", bp.get_editor_property('parent_class'))

# Check SCS (SimpleConstructionScript)
scs = bp.get_editor_property('simple_construction_script')
print("SCS:", scs)
if scs:
    all_nodes = scs.get_all_nodes()
    print("SCS nodes count:", len(all_nodes))
    for n in all_nodes:
        print(" - Node:", n.get_component_template().get_name(), n.get_component_template().get_class().get_name())
