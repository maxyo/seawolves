// expect-error: native declaration of built-in function 'GetDeltaTime' returns float, the engine's returns integer
native float GetDeltaTime();

int test_none()
{
	return 1;
}
