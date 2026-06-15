from glob import glob
import os

from setuptools import find_packages, setup

package_name = "k1_robot_webui_client"

setup(
    name=package_name,
    version="0.0.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", [os.path.join("resource", package_name)]),
        (os.path.join("share", package_name), ["package.xml"]),
        (os.path.join("share", package_name, "config"), glob("config/*.yaml")),
        (os.path.join("share", package_name, "launch"), glob("launch/*.py")),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="booster",
    maintainer_email="todo@todo.todo",
    description="HTTP bridge that reports robot status snapshots to a LAN WebUI backend.",
    license="Apache-2.0",
    tests_require=["pytest"],
    entry_points={
        "console_scripts": [
            "webui_client_node = k1_robot_webui_client.webui_client_node:main",
        ],
    },
)
