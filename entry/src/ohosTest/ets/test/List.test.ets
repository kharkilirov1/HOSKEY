import PredictionModelTest from './PredictionModel.test'
import KeyboardKeyDataTest from './KeyboardKeyData.test'
import abilityTest from './Ability.test';

export default function testsuite() {
  abilityTest();
  KeyboardKeyDataTest()
  PredictionModelTest()
}