/**
 * @file lstm_baseline_main.cpp
 * @author Konstantin Sidorov
 *
 * Executable for LSTM baseline solution for ann::augmented tasks.
 *
 * mlpack is free software; you may redistribute it and/or modify it under the
 * terms of the 3-clause BSD license.  You should have received a copy of the
 * 3-clause BSD license along with mlpack.  If not, see
 * http://www.opensource.org/licenses/BSD-3-Clause for more information.
 */
#include <mlpack/prereqs.hpp>
#include <mlpack/core/util/cli.hpp>
#include <mlpack/core/math/random.hpp>

#include <mlpack/core/data/binarize.hpp>

#include <mlpack/methods/ann/augmented/tasks/copy.hpp>
#include <mlpack/methods/ann/augmented/tasks/sort.hpp>
#include <mlpack/methods/ann/augmented/tasks/add.hpp>
#include <mlpack/methods/ann/augmented/tasks/score.hpp>

#include <mlpack/core/optimizers/adam/adam.hpp>
#include <mlpack/methods/ann/layer/layer.hpp>
#include <mlpack/methods/ann/layer/leaky_relu.hpp>
#include <mlpack/methods/ann/rnn.hpp>

using namespace mlpack;

using namespace mlpack::ann::augmented::tasks;
using namespace mlpack::ann::augmented::scorers;

using namespace mlpack::ann;
using namespace mlpack::optimization;

using mlpack::data::Binarize;

using std::string;
using std::vector;
using std::pair;
using std::make_pair;

PROGRAM_INFO("LSTM baseline for ann::augmented tasks",
    "This program runs and evaluates a simple LSTM recurrent neural network "
    "over several standard tasks from ann::augmented::tasks."
    "\n\n"
    "For example, the following will execute the LSTM network on sequence copy task instance "
    "with 6 as maximum sequence length, using 1000 samples for learning and running 1000 "
    "epochs over them:"
    "\n\n"
    "$ augmented_baseline --task=copy --epochs=1000 --samples=1000 --length=6 --repeats=1"
    "\n\n"
    "Supported tasks are listed below:"
    "\n\n"
    " * 'copy': sequence copy task\n"
    "\n"
    " * 'add': binary addition task\n"
    "\n"
    " * 'sort': sequence sort task (sequence elements are binary numbers)\n"
    "\n"
    "The parameters for each of the tasks should be specified with the "
    "options --length, --bit_length, or a "
    "combination of those options (as in case with --task=sort)."
    "\n\n"
    "Learning-specific parameters are also tunable (e.g., --epochs and --samples)");

PARAM_STRING_IN_REQ("task", "Task to execute LSTM network on.", "t");

PARAM_INT_IN("length", "Maximum sequence length (doesn't impact binary addition task).", "l", 2);
PARAM_INT_IN("repeats", "Number of repeats required to solve the task (specific for Copy task).", "r", 1);
PARAM_INT_IN("bit_length", "Maximum length of sequence elements in binary representation.", "b", 2);
PARAM_INT_IN("epochs", "Learning epochs.", "e", 25);
PARAM_INT_IN("samples", "Sample size used for fitting and evaluating the model.", "s", 64);

// Generate the LSTM model for benchmarking.
// NOTE: it's the same model for all tasks.
RNN<MeanSquaredError<> > GenerateModel(size_t inputSize,
                                       size_t outputSize,
                                       size_t maxRho)
{
  RNN<MeanSquaredError<> > model(2);

  model.Add<IdentityLayer<> >();
  model.Add<Linear<> >(inputSize, 40);
  model.Add<LeakyReLU<> >();
  model.Add<LSTM<> >(40, 30, maxRho);
  model.Add<LeakyReLU<> >();
  model.Add<LSTM<> >(30, 30, maxRho);
  model.Add<LeakyReLU<> >();
  model.Add<LSTM<> >(30, 20, maxRho);
  model.Add<LeakyReLU<> >();
  model.Add<Linear<> >(20, outputSize);
  model.Add<SigmoidLayer<> >();

  return model;
}

//! Runs an instance of the task of given size on a baseline LSTM model.
template<typename TaskType>
void RunTask(TaskType& task,
             size_t inputSize,
             size_t outputSize,
             size_t epochs,
             size_t samples)
{
  // TODO Make this dirty hack less dirty.
  size_t maxRho = inputSize * 1024 + 1;

  // Creating a baseline model.
  RNN<MeanSquaredError<> > model = GenerateModel(inputSize, outputSize, maxRho);
  Adam opt;
  opt.MaxIterations() = epochs * samples;
  opt.Tolerance() = 0;

  // Generating a task instance
  arma::mat trainPredictor, trainResponse;
  task.Generate(trainPredictor, trainResponse, samples);

  arma::field<arma::mat> testPredictor, testResponse;
  task.Generate(testPredictor, testResponse, samples, true);
  assert(testPredictor.n_elem == testResponse.n_elem &&
         testResponse.n_elem == samples);

  // Training loop
  model.Rho() = trainPredictor.n_rows / inputSize;
  model.Train(trainPredictor, trainResponse, opt);

  // Evaluation loop
  Log::Info << "Running evaluation loop.\n";
  arma::field<arma::mat> modelOutput(samples);

  for (size_t example = 0; example < samples; ++example) {
    arma::mat predictor = testPredictor.at(example);
    arma::mat response = testResponse.at(example);

    Log::Debug << "Evaluating model on:\n";
    Log::Debug << "Input sequence:\n" << predictor.t();
    Log::Debug << "Ground truth sequence:\n" << response.t();

    model.Rho() = predictor.n_elem / inputSize;
    arma::mat softOutput;
    model.Predict(
      predictor,
      softOutput);

    Log::Debug << "Model predictions:\n";
    Log::Debug << softOutput.t();

    modelOutput.at(example) = softOutput;
    Binarize<double>(modelOutput.at(example), modelOutput.at(example), 0.5);

    Log::Debug << "Model predictions after binarization:\n";
    Log::Debug << testResponse.at(example).t();
  }
  Log::Info << "Final score: "
            << SequencePrecision<arma::mat>(testResponse, modelOutput)
            << "\n";
  std::cout << "Final score: "
            << SequencePrecision<arma::mat>(testResponse, modelOutput)
            << "\n";
}

int main(int argc, char** argv)
{
  // Parse command line options.
  CLI::ParseCommandLine(argc, argv);

  string task = CLI::GetParam<string>("task");
  int repeats = CLI::GetParam<int>("repeats");
  int bitLen = CLI::GetParam<int>("bit_length");
  int maxLen = CLI::GetParam<int>("length");
  int epochs = CLI::GetParam<int>("epochs");
  int samples = CLI::GetParam<int>("samples");
  vector<pair<string, int>> params = {
      make_pair("repeats", repeats),
      make_pair("bit_length", bitLen),
      make_pair("length", maxLen),
      make_pair("epochs", epochs),
      make_pair("samples", samples)
  };
  for (pair<string, int> param : params) {
    if (param.second <= 0) {   
      Log::Fatal << "Invalid value for '" << param.first << "': "
                 << "expecting a positive number, received "
                 << param.second << ".\n";
    }
  }
  if (task == "copy") {
    CopyTask task(maxLen, repeats, true);
    RunTask<CopyTask>(task, 2, 1, epochs, samples);
  }
  else if (task == "add") {
    AddTask task(bitLen);
    RunTask<AddTask>(task, 3, 3, epochs, samples);
  }
  else if (task == "sort") {
    SortTask task(maxLen, bitLen);
    RunTask<SortTask>(task, bitLen, bitLen, epochs, samples);
  }
  else {
    Log::Fatal << "Can't recognize task type, aborting execution.\n"
               << "Supported tasks: add, copy, sort.\n";
  }
}
