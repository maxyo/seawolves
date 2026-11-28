// profile: Caribbean Legend
// an aref function that returns nullptr for a missing attribute, its result compared with nullptr (a stealth check of
// every colony interior has this shape)
object registry;

aref FindAttribute(ref owner, string path, bool create = false)
{
	if (!CheckAttribute(owner, path) && !create) return nullptr;

	aref result;
	makearef(result, owner.(path));
	return result;
}

int test_missing_attribute_is_nullptr()
{
	aref info = FindAttribute(&registry, "colonies.none");
	if (info != nullptr) return 0;
	return 1;
}

int test_present_attribute_is_not_nullptr()
{
	registry.colonies.here = "1";
	aref info = FindAttribute(&registry, "colonies.here");
	return info != nullptr;
}
