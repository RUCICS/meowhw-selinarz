[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/qPRVdnK4)
# Meowlab

这个lab的全部内容都在`meowlab.ipynb`中，并且需要你运行在linux或者WSL中。

## 安装环境

因为这个lab需要使用jupyter notebook，我们强烈建议你使用uv来管理你的项目。首先，你需要安装`uv`。使用下面这个命令来一键安装。

    curl -LsSf https://astral.sh/uv/install.sh | sh

由于我们已经为你准备好了配置文件，所以你可以直接使用以下命令来安装jupyter notebook需要的环境。

    uv sync

最后，我们建议你在vscode中安装jupyter插件，来更方便地使用jupyter notebook。

如果以上安装过程发生了问题，你也可以使用`pip install jupyter`来手动安装jupyter notebook。