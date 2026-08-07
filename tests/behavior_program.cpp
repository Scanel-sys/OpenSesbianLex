int main()
{
    int input_value = 3;
    int result_value = 0;
    int condition_evaluations = 0;

    if (++condition_evaluations == 1)
    {
        result_value = input_value * 10 + condition_evaluations;
    }

    return result_value + condition_evaluations;
}
