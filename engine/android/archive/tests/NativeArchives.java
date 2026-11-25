package org.stormengine.port;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.zip.*;
import java.io.*;
/** Host JVM check of the JNI bridge: UTF-16 paths, results, staged progress and cancellation. */
public final class NativeArchives {
 static {System.loadLibrary("storm_archive");}
 interface Progress {boolean onProgress(int stage,String path,long bytes,long total,long files);}
 static native String[] discover(String source,String scratch,String password,String layer,long[] limits,Progress progress);
 static native String[] patchLayer(String layer);
 static native String[] extractRoot(String source,String scratch,String password,String id,String staging,long[] limits,Progress progress);
 static native String[] removeTree(String path);
 static native String checkSave(String path);
 static final long[] LIMITS={64L<<30,4L<<30,400000,3600,32L<<30,3,100,1<<20};
 static void require(boolean ok,String what){if(!ok)throw new AssertionError(what);}
 static String field(String json,String name){int at=json.indexOf("\""+name+"\":\"")+name.length()+4;return json.substring(at,json.indexOf('"',at));}
 public static void main(String[] args)throws Exception {
  Path root=Files.createTempDirectory("storm-jni-🚢-");
  try {
   Path source=root.resolve("источник-🚢"),scratch=root.resolve("scratch"),out=root.resolve("output-🚢");
   Files.createDirectory(source);Files.createDirectory(scratch);Files.createDirectory(out);
   Path input=source.resolve("игра-🚢.zip");
   String name="RESOURCE/Корабль-🚢.txt";
   try(ZipOutputStream zip=new ZipOutputStream(Files.newOutputStream(input))){
    for(String file:new String[]{name,"PROGRAM/game.c","engine.ini"}){
     zip.putNextEntry(new ZipEntry("Корсары/"+file));zip.write(file.getBytes(StandardCharsets.UTF_8));zip.closeEntry();
    }
   }
   String[] info=discover(source.toString(),scratch.toString(),null,null,new long[]{1,2,3},null);
   require(info[0].equals("6")&&info.length==3,"Invalid limits are explicit: "+info[0]);
   final boolean[] reading={false};
   info=discover(source.toString(),scratch.toString(),null,null,LIMITS,(stage,path,bytes,total,files)->{reading[0]|=stage==1;return false;});
   require(info[0].equals("0"),"discover: "+info[1]);require(reading[0],"Reading stage reported");
   require(info[2].contains("\"root\":\"Корсары/\"")&&info[2].contains("игра-🚢.zip"),"Discovery UTF-16 round trip: "+info[2]);
   require(info[2].contains("\"identified\":false,\"patch_set\":null")&&info[2].contains("\"patch_sets\":null"),"No layer: nothing identified: "+info[2]);
   // A layer path (UTF-16 round trip): the set's identify file is missing from this copy, so no set identifies it.
   Path layer=root.resolve("слой-🚢"),set=layer.resolve("sets/s-1");Files.createDirectories(set);
   Files.writeString(set.resolve("index.txt"),"storm-script-patches\t1\nset\ts-1\tИгра 🚢\t1.0\nidentify\tPROGRAM/v.h\t"+"0".repeat(64)+"\n");
   String[] sets=patchLayer(layer.toString());
   require(sets[0].equals("0")&&sets[2].contains("\"game\":\"Игра 🚢\"")&&sets[2].contains("\"identify\":[\"program/v.h\"]"),"patchLayer: "+String.join("|",sets));
   try(var files=Files.list(scratch)){require(files.count()==0,"Scratch empty after a plain archive");}
   info=discover(source.toString(),scratch.toString(),null,layer.toString(),LIMITS,null);
   require(info[0].equals("0")&&info[2].contains("\"identified\":true,\"patch_set\":null")&&info[2].contains("\"version\":\"1.0\""),"Identified without a match: "+info[2]);
   require(discover(source.toString(),scratch.toString(),null,root.resolve("absent").toString(),LIMITS,null)[0].equals("1"),"Absent layer is an IO failure");
   String id=field(info[2],"id");
   info=extractRoot(source.toString(),scratch.toString(),null,id,out.toString(),LIMITS,(stage,path,bytes,total,files)->files>=1);
   require(info[0].equals("7"),"Callback true cancels: "+info[0]);
   try(var files=Files.walk(out)){files.sorted(java.util.Comparator.reverseOrder()).filter(p->!p.equals(out)).forEach(p->p.toFile().delete());}
   final boolean[] observed={false};
   info=extractRoot(source.toString(),scratch.toString(),null,id,out.toString(),LIMITS,(stage,path,bytes,total,files)->{if(stage==3&&path.equals(name))observed[0]=true;return false;});
   require(info[0].equals("0"),"extractRoot: "+info[1]);require(observed[0],"Callback Unicode");
   require(Files.readString(out.resolve(name)).equals(name),"Output Unicode path and bytes");
   // A ZIP without the UTF-8 flag, names in the DOS code page (as Windows writes them on a Russian system): the
   // reader writes the UTF-8 names, the ones Java lists.
   Path dos=root.resolve("dos"),dosScratch=root.resolve("dos-scratch"),dosOut=root.resolve("dos-out");
   Files.createDirectory(dos);Files.createDirectory(dosScratch);Files.createDirectory(dosOut);
   try(ZipOutputStream zip=new ZipOutputStream(Files.newOutputStream(dos.resolve("dos.zip")),java.nio.charset.Charset.forName("IBM866"))){
    for(String file:new String[]{"RESOURCE/tavern_ГПК.tga.tx","PROGRAM/game.c","engine.ini"}){
     zip.putNextEntry(new ZipEntry("Игра/"+file));zip.write(file.getBytes(StandardCharsets.UTF_8));zip.closeEntry();
    }
   }
   info=discover(dos.toString(),dosScratch.toString(),null,null,LIMITS,null);
   require(info[0].equals("0")&&info[2].contains("\"root\":\"Игра/\""),"DOS names discovered: "+String.join("|",info));
   info=extractRoot(dos.toString(),dosScratch.toString(),null,field(info[2],"id"),dosOut.toString(),LIMITS,null);
   require(info[0].equals("0"),"DOS names extracted: "+info[1]);
   require(java.util.Arrays.asList(dosOut.resolve("RESOURCE").toFile().list()).equals(java.util.Arrays.asList("tavern_ГПК.tga.tx")),"DOS name as UTF-8");
   // removeTree: a tree with names that are not UTF-8 (made by sh: a Java string cannot spell them).
   Path raw=root.resolve("raw");
   require(new ProcessBuilder("sh","-c","d=\"$0/$(printf 'a\\203b')\" && mkdir -p \"$d/c\" && : > \"$d/c/$(printf '\\377')\"",raw.toString()).inheritIO().start().waitFor()==0,"raw tree made");
   String[] removed=removeTree(raw.toString());
   require(removed.length==2&&removed[0].equals("0")&&!Files.exists(raw),"removeTree: "+String.join("|",removed));
   // checkSave: the engine's save reader over a UTF-16 path; a file that is not a save is refused with the field.
   Path notSave=root.resolve("сохранение-🚢");Files.write(notSave,new byte[64]);
   String why=checkSave(notSave.toString());
   require(why!=null&&why.contains("packedSize 0")&&Files.size(notSave)==64,"checkSave: "+why);
   require(checkSave(root.resolve("нет").toString())!=null,"checkSave of a missing file");
   System.out.println("JNI Unicode paths, discovery, staged progress, cancellation, root extraction, DOS names, tree removal and the save check passed");
  } finally {
   try(var files=Files.walk(root)){files.sorted(java.util.Comparator.reverseOrder()).forEach(p->{try{Files.delete(p);}catch(IOException e){throw new RuntimeException(e);}});}
  }
 }
}
