export const metadata = {
  title: "Arista RRM Playground",
  description: "RRM Playground - Standalone Application",
};

export default function PlaygroundLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  // This layout is nested inside the root layout
  // It doesn't need html/body tags or Providers (handled by root layout)
  // It just provides a clean wrapper for the playground route
  return (
    <>
      {children}
    </>
  );
}


