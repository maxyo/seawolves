// profile: Caribbean Legend
// nullptr as the context of a text native gives no context
native string DLGO(string sInput, ...);

int test_null_context()
{
	return DLGO("plain", nullptr) == "plain";
}
