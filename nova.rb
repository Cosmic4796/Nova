class Nova < Formula
  desc "A modern, dynamically-typed programming language implemented in C"
  homepage "https://github.com/Cosmic4796/Nova"
  url "https://github.com/Cosmic4796/Nova/archive/refs/tags/v0.2.0.tar.gz"
  sha256 "PLACEHOLDER_SHA256"
  license "GPL-3.0"

  depends_on "cmake" => :build
  depends_on "curl" => :optional

  def install
    system "cmake", "-B", "build", *std_cmake_args
    system "cmake", "--build", "build"
    bin.install "build/nova"
    (lib/"nova/stdlib").install Dir["build/stdlib/*"]
  end

  test do
    assert_match "Nova v", shell_output("#{bin}/nova version")
    (testpath/"hello.nova").write('print("Hello, Homebrew!")')
    assert_equal "Hello, Homebrew!\n", shell_output("#{bin}/nova #{testpath}/hello.nova")
  end
end
