#include "TzRule.h"
#include <assert.h>
int main() {
  assert(tzRuleValid("ICT-7") && tzRuleValid("<+0530>-5:30") && tzRuleValid("CET-1CEST,M3.5.0,M10.5.0/3"));
  assert(tzRuleValid("NZST-12NZDT,M9.5.0,M4.1.0/3"));
  assert(!tzRuleValid(nullptr) && !tzRuleValid("") && !tzRuleValid("7") && !tzRuleValid("-7"));
  assert(!tzRuleValid("ICT 7") && !tzRuleValid("ICT-7;rm") && !tzRuleValid("ICT\"-7") && !tzRuleValid("ICT-7\n"));
  assert(!tzRuleValid("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ"));   // too long
  return 0;
}
