// PSX XMB Manager 72713da7, GameEditor + Utils.GetResizedBitmap.
// Use its exact nQuant 1.0.3 assembly, not a substitute palette algorithm.
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Security.Cryptography;
using nQuant;

class XmbJackets {
    static string Hash(string path) {
        using (var sha = SHA256.Create())
            return BitConverter.ToString(sha.ComputeHash(File.ReadAllBytes(path))).Replace("-", "").ToLowerInvariant();
    }
    static void Convert(string source, string target, int width, int height) {
        using (var original = new Bitmap(source))
        using (var resized = new Bitmap(original, new Size(width, height))) {
            if (resized.PixelFormat != PixelFormat.Format32bppArgb)
                throw new InvalidOperationException("Unexpected System.Drawing resize format");
            using (var indexed = new WuQuantizer().QuantizeImage(resized)) {
                if (indexed == null || indexed.PixelFormat != PixelFormat.Format8bppIndexed)
                    throw new InvalidOperationException("nQuant did not produce an 8-bit indexed image");
                Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(target)));
                indexed.Save(target, ImageFormat.Png);
            }
        }
    }
    static int Main(string[] args) {
        try {
            if (args.Length == 3) {
                Convert(args[0], args[1], 140, 200);
                Convert(args[0], args[2], 74, 108);
                // Also permit native Linux servers to serve pairs prepared on Windows.
                // The server's temporary outputs do not receive standalone stamps.
                if (args[2].EndsWith(".png", StringComparison.OrdinalIgnoreCase))
                    File.WriteAllText(args[2] + ".xmb-manager", "system-drawing-nquant-1.0.3-v1\n" + Hash(args[1]) + "\n" + Hash(args[2]) + "\n",
                                      new System.Text.UTF8Encoding(false));
            } else if (args.Length == 4) {
                Convert(args[0], args[1], int.Parse(args[2]), int.Parse(args[3]));
            } else { throw new ArgumentException("source large.png small.png OR source output.png width height"); }
            return 0;
        } catch (Exception e) { Console.Error.WriteLine(e.Message); return 1; }
    }
}
