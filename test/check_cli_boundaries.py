#!/usr/bin/env python3
"""Additional CLI input boundary fixtures; run with the Jazzy and workspace environment sourced."""
import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import zipfile
import rosbag2_py
from rclpy.serialization import serialize_message
from nav_msgs.msg import Path as RosPath
from geometry_msgs.msg import PoseStamped
from std_msgs.msg import String

parser=argparse.ArgumentParser()
parser.add_argument("--executable",required=True)
parser.add_argument("--work",required=True)
args=parser.parse_args()
work=Path(args.work);work.mkdir(parents=True,exist_ok=True)
passed=[]
with tempfile.TemporaryDirectory(prefix="inputs_",dir=work) as directory:
    root=Path(directory)
    def run(*arguments):
        return subprocess.run([args.executable,*map(str,arguments)],capture_output=True,text=True,timeout=30)
    for name,entries in [("links",[("bag/link",True)]),("ambiguous",[("a/metadata.yaml",False),("b/metadata.yaml",False)])]:
        path=root/(name+".zip")
        with zipfile.ZipFile(path,"w") as archive:
            for entry,link in entries:
                info=zipfile.ZipInfo(entry);info.create_system=3
                info.external_attr=((stat.S_IFLNK if link else stat.S_IFREG)|0o600)<<16
                archive.writestr(info,"../outside" if link else "invalid")
        result=run("inspect","--bag",path)
        assert result.returncode==1,(name,result.stdout,result.stderr)
        assert "unsafe ZIP" in result.stderr if name=="links" else "exactly one bag" in result.stderr
        passed.append(name)
    def writer(path,type_name):
        result=rosbag2_py.SequentialWriter()
        result.open(rosbag2_py.StorageOptions(uri=str(path),storage_id="mcap"),rosbag2_py.ConverterOptions("cdr","cdr"))
        result.create_topic(rosbag2_py.TopicMetadata(id=0,name="/plan",type=type_name,serialization_format="cdr"))
        return result
    bag=root/"unsupported";w=writer(bag,"std_msgs/msg/String")
    w.write("/plan",serialize_message(String(data="unrecognized path type")),1792655909000000000)
    del w
    result=run("inspect","--bag",bag)
    assert result.returncode==2,result.stderr
    data=json.loads(result.stdout)
    assert any(d["code"]=="unsupported_type" for d in data["diagnostics"])
    passed.append("unsupported_topic_type")
    bag=root/"duplicates";w=writer(bag,"nav_msgs/msg/Path")
    path=RosPath();path.header.frame_id="map";path.header.stamp.sec=1792655909
    for x in [0.,1.]:
        pose=PoseStamped();pose.header=path.header;pose.pose.orientation.w=1.;pose.pose.position.x=x;path.poses.append(pose)
    for offset in [1,2]:w.write("/plan",serialize_message(path),1792655909000000000+offset)
    path.poses[0].pose.position.x=float("nan")
    w.write("/plan",serialize_message(path),1792655909000000003);del w
    result=run("inspect","--bag",bag)
    assert result.returncode==0,result.stderr
    data=json.loads(result.stdout);assert data["path_publications"]==3
    passed.append("same_millisecond_duplicate_and_invalid_path")
    log=root/"wrong.log";log.write_text("[bt_navigator] [100.000000000] Begin navigating from current location (0, 0) to (1, 1)\n")
    result=run("inspect","--bag",bag,"--log",log)
    data=json.loads(result.stdout);assert not data["meta"]["log_compatible"] and data["navigation_tasks"]==0
    passed.append("mismatched_log")
    invalid=root/"invalid.json";invalid.write_text('{"schema_version":99,"generator":{"name":"Track Insight"}}')
    result=run("render","--analysis",invalid,"--output",root/"out")
    assert result.returncode==1 and not (root/"out").exists()
    passed.append("schema_rejection")
print(json.dumps({"status":"passed","cases":passed}))
