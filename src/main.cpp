#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>


#include "core/shell.hpp"
#include "interpreter/script_debugger.hpp"
#include "interpreter/script_interpreter.hpp"

void print_version() {
  std::cout << "Modern Shell v1.0.0" << std::endl;
  std::cout << "Built with C++20" << std::endl;
}

void print_usage(const std::string &prog_name) {
  std::cout << "Usage: " << prog_name << " [OPTIONS] [SCRIPT]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -c COMMAND   Execute COMMAND and exit" << std::endl;
  std::cout << "  -h, --help   Display this help message" << std::endl;
  std::cout << "  -v, --version Display version information" << std::endl;
  std::cout << "  --no-color   Disable colored output" << std::endl;
  std::cout << "  --script     Start in script interpreter mode" << std::endl;
  std::cout << "  --debug      Enable script debugging features" << std::endl;
}

// 检查和创建必要的配置目录
void setup_config_directories() {
// 获取用户主目录
#ifdef _WIN32
  const char *home_env = getenv("USERPROFILE");
#else
  const char *home_env = getenv("HOME");
#endif

  if (!home_env) {
    std::cerr << "警告: 无法确定用户主目录，将跳过创建配置目录\n";
    return;
  }

  std::filesystem::path home_dir(home_env);
  std::filesystem::path config_dir = home_dir / ".lithium";
  std::filesystem::path plugins_dir = config_dir / "plugins";
  std::filesystem::path scripts_dir = config_dir / "scripts";

  // 创建配置目录结构
  try {
    if (!std::filesystem::exists(config_dir)) {
      std::filesystem::create_directory(config_dir);
      std::cout << "创建配置目录: " << config_dir << std::endl;
    }

    if (!std::filesystem::exists(plugins_dir)) {
      std::filesystem::create_directory(plugins_dir);
      std::cout << "创建插件目录: " << plugins_dir << std::endl;
    }

    if (!std::filesystem::exists(scripts_dir)) {
      std::filesystem::create_directory(scripts_dir);
      std::cout << "创建脚本目录: " << scripts_dir << std::endl;
    }

    // 创建默认配置文件（如果不存在）
    std::filesystem::path config_file = config_dir / "config.json";
    if (!std::filesystem::exists(config_file)) {
      std::ofstream file(config_file);

      if (file.is_open()) {
        file
            << "{\n"
            << "  \"prompt\": \"\\033[1;32m➜ \\033[1;34m{pwd}\\033[0m$ \",\n"
            << "  \"history_size\": 1000,\n"
            << "  \"plugins\": {\n"
            << "    \"enabled\": [\"autocomplete\", \"history\", "
               "\"syntax_highlight\", \"git_integration\", \"file_watcher\"],\n"
            << "    \"auto_load\": true,\n"
            << "    \"hot_reload\": true\n"
            << "  },\n"
            << "  \"script\": {\n"
            << "    \"auto_completion\": true,\n"
            << "    \"line_numbers\": true,\n"
            << "    \"syntax_highlight\": true,\n"
            << "    \"show_debug_info\": false\n"
            << "  }\n"
            << "}\n";
        file.close();
        std::cout << "创建默认配置文件: " << config_file << std::endl;
      }
    }
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "错误: 创建配置目录时发生错误: " << e.what() << std::endl;
  }
}

// 处理命令行参数
struct CommandLineArgs {
  bool interactive = true;
  std::string script_file;
  bool no_plugins = false;
  bool version = false;
  bool help = false;
  bool script_mode = false; // 脚本解释器模式
  bool debug_mode = false;  // 调试模式
};

CommandLineArgs parse_args(int argc, char *argv[]) {
  CommandLineArgs args;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "-c" || arg == "--command") {
      args.interactive = false;
      // 直接执行命令
      if (i + 1 < argc) {
        args.script_file = argv[++i];
      }
    } else if (arg == "--no-plugins") {
      args.no_plugins = true;
    } else if (arg == "-v" || arg == "--version") {
      args.version = true;
    } else if (arg == "-h" || arg == "--help") {
      args.help = true;
    } else if (arg == "--script") {
      args.script_mode = true;
    } else if (arg == "--debug") {
      args.debug_mode = true;
    } else if (!arg.empty() && arg[0] != '-') {
      // 假定是脚本文件
      args.interactive = false;
      args.script_file = arg;
    }
  }

  return args;
}

// 显示版本信息
void show_version() {
  std::cout << "Lithium Shell 版本 1.0.0\n";
  std::cout << "使用C++20标准构建\n";
  std::cout << "Copyright (C) 2025 Lithium Shell 团队\n";
}

// 显示帮助信息
void show_help() {
  std::cout << "使用方法: lithium [选项] [脚本]\n\n";
  std::cout << "选项:\n";
  std::cout << "  -c, --command <命令>    执行给定命令并退出\n";
  std::cout << "  --no-plugins           禁用插件系统\n";
  std::cout << "  --script               启动脚本解释器模式\n";
  std::cout << "  --debug                启用调试功能\n";
  std::cout << "  -v, --version          显示版本信息\n";
  std::cout << "  -h, --help             显示此帮助信息\n";
}

int main(int argc, char *argv[]) {
  // 解析命令行参数
  auto args = parse_args(argc, argv);

  if (args.version) {
    show_version();
    return 0;
  }

  if (args.help) {
    show_help();
    return 0;
  }

  // 创建必要的配置目录
  setup_config_directories();

  // 创建shell实例
  auto shell = std::make_unique<shell::Shell>();
  shell->initialize();

  // 如果指定了禁用插件，则在初始化后关闭插件系统
  if (args.no_plugins) {
    std::cout << "已禁用插件系统\n";
    // 卸载所有插件
    shell->unload_all_plugins();
  }

  int exit_status = 0;

  try {
    if (args.script_mode) {
      // 进入脚本解释器模式
      std::cout << "启动脚本解释器模式...\n";
      
      // 如果需要调试功能，启用调试器
      if (args.debug_mode) {
        auto& debugger = shell->get_script_interpreter().get_debugger();
        debugger.start();
        std::cout << "调试功能已启用\n";
      }
      
      shell->start_script_repl();
      exit_status = shell->get_environment().get_last_exit_status();
    } else if (args.interactive) {
      // 进入交互式模式
      shell->run();
      exit_status = shell->get_environment().get_last_exit_status();
    } else {
      // 非交互式模式 - 执行脚本或命令
      std::string result;

      if (!args.script_file.empty()) {
        if (args.script_file[0] == '-' && args.script_file[1] == 'c') {
          // 执行单个命令（从-c选项后面的参数）
          std::string command = args.script_file.substr(2);
          if (command.empty() && argc > 2) {
            command = argv[2];
          }
          result = shell->evaluate(command);
        } else {
          // 执行脚本文件
          // 检查是否以.lith或.script结尾，如果是则使用脚本解释器执行
          std::filesystem::path script_path(args.script_file);
          std::string ext = script_path.extension().string();
          
          if (ext == ".lith" || ext == ".script") {
            // 使用脚本解释器执行
            // 如果需要调试功能，启用调试器
            if (args.debug_mode) {
              auto& debugger = shell->get_script_interpreter().get_debugger();
              debugger.start();
              // 在脚本第一行设置断点
              debugger.add_breakpoint(script_path.string(), 1);
              std::cout << "在脚本第1行设置断点并启用调试模式\n";
            }
            
            auto script_result = shell->get_script_interpreter().load_and_eval(script_path);
            if (std::holds_alternative<shell::ScriptError>(script_result)) {
              std::cout << std::get<shell::ScriptError>(script_result).format_error() << std::endl;
              exit_status = 1;
            }
          } else {
            // 使用常规shell模式执行
            result = shell->evaluate_script(args.script_file);
          }
        }
      }

      if (!result.empty()) {
        std::cout << result << std::endl;
      }

      exit_status = shell->get_environment().get_last_exit_status();
    }
  } catch (const std::exception &e) {
    std::cerr << "错误: " << e.what() << std::endl;
    exit_status = 1;
  }

  // 确保Shell正确关闭，触发插件的关闭事件
  shell->shutdown();

  return exit_status;
}